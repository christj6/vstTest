#include "WavetableImporter.h"

namespace boringsynth
{

namespace
{

// How many whole blocks must finish after a table's replaced before it's freed: the one that may already
// have been under way, and one that started with the new table in place.
constexpr juce::uint64 blocksBeforeFreeing = 2;

// And how many samples: the fades that can still read the old table last 15 ms, and this is over 100 ms
// at sample rates up to 192 kHz.
constexpr juce::uint64 samplesBeforeFreeing = 20000;

// How often to check whether an old table can be freed, in milliseconds.
constexpr int freeingCheckMs = 200;

constexpr auto sineRms = 0.70710678f;
constexpr auto maxPeak = 3.0f;
constexpr auto silentRms = 1.0e-6f;

// The whole file as one channel, its channels mixed. Anything that isn't a number becomes silence.
std::vector<float> readSamples (juce::AudioFormatReader& reader)
{
    const auto numSamples = (int) reader.lengthInSamples;
    juce::AudioBuffer<float> buffer ((int) reader.numChannels, numSamples);
    reader.read (&buffer, 0, numSamples, 0, true, true);

    std::vector<float> samples ((size_t) numSamples, 0.0f);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < numSamples; ++i)
            samples[(size_t) i] += buffer.getSample (channel, i) / (float) buffer.getNumChannels();

    for (auto& sample : samples)
        if (! std::isfinite (sample))
            sample = 0.0f;

    return samples;
}

// One cycle of any length as WaveTable::size samples, along straight lines, as with reading a table.
std::vector<float> resampleCycle (const std::vector<float>& cycle)
{
    std::vector<float> resampled ((size_t) WaveTable::size);

    for (int i = 0; i < WaveTable::size; ++i)
    {
        const auto position = (double) i * (double) cycle.size() / WaveTable::size;
        const auto index = (size_t) position;
        const auto fraction = (float) (position - (double) index);
        const auto next = cycle[(index + 1) % cycle.size()];
        resampled[(size_t) i] = cycle[index] + fraction * (next - cycle[index]);
    }

    return resampled;
}

// Removes a frame's DC offset, and scales it to the sine's RMS, its peak at most maxPeak.
void matchFrame (std::vector<float>& frame)
{
    const auto numSamples = (float) frame.size();
    auto sum = 0.0f;

    for (auto sample : frame)
        sum += sample;

    auto sumOfSquares = 0.0f, peak = 0.0f;

    for (auto& sample : frame)
    {
        sample -= sum / numSamples;
        sumOfSquares += sample * sample;
        peak = std::max (peak, std::abs (sample));
    }

    const auto rms = std::sqrt (sumOfSquares / numSamples);
    const auto gain = rms < silentRms ? 0.0f : std::min (sineRms / rms, maxPeak / peak);

    for (auto& sample : frame)
        sample *= gain;
}

} // namespace

WavetableImporter::WavetableImporter()
    : juce::Thread ("BoringSynth wavetable import")
{
}

WavetableImporter::~WavetableImporter()
{
    // A file part-way through loading finishes first, which can take a few seconds in a Debug build.
    stopThread (30000);
}

void WavetableImporter::import (const juce::File& file)
{
    {
        const juce::ScopedLock sl (lock);
        pendingFile = file;
        busy = true;
    }

    if (! isThreadRunning())
        startThread();

    notify();
}

WavetableImporter::Status WavetableImporter::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void WavetableImporter::readTable (const std::function<void (const MultiFrameWaveTable*)>& reader) const
{
    const juce::ScopedLock sl (lock);
    reader (table.get());
}

void WavetableImporter::blockRendered (int numSamples) noexcept
{
    samplesRendered.fetch_add ((juce::uint64) numSamples, std::memory_order_relaxed);
    blocksRendered.fetch_add (1, std::memory_order_relaxed);
}

int WavetableImporter::getNumOldTables() const
{
    const juce::ScopedLock sl (lock);
    return (int) oldTables.size();
}

void WavetableImporter::run()
{
    while (! threadShouldExit())
    {
        juce::File file;

        {
            const juce::ScopedLock sl (lock);
            std::swap (file, pendingFile);
        }

        if (file != juce::File())
            loadFile (file);

        freeOldTables();

        const auto anyWaiting = [this]
        {
            const juce::ScopedLock sl (lock);

            if (pendingFile == juce::File())
                busy = false;

            return ! oldTables.empty();
        }();

        wait (anyWaiting ? freeingCheckMs : -1);
    }
}

void WavetableImporter::loadFile (const juce::File& file)
{
    auto read = readCycles (file);

    if (read.frames.empty())
    {
        const juce::ScopedLock sl (lock);
        status.error = file.getFileName() + ": " + read.error;
        return;
    }

    auto newTable = std::make_unique<MultiFrameWaveTable> (read.frames);
    publish (std::move (newTable), std::move (read.frames), { file.getFileName(), {} });
}

void WavetableImporter::publish (std::unique_ptr<MultiFrameWaveTable> newTable, std::vector<std::vector<float>> newCycles, const Status& newStatus)
{
    {
        const juce::ScopedLock sl (lock);

        if (table != nullptr)
            oldTables.push_back ({ std::move (table), blocksRendered.load(), samplesRendered.load() });

        table = std::move (newTable);
        cycles = std::move (newCycles);
        current.store (table.get(), std::memory_order_release);
        status = newStatus;
        ++generation;
    }

    // The thread frees the old table, once it's safe.
    if (! isThreadRunning())
        startThread();

    notify();
}

void WavetableImporter::clear()
{
    if (getTable() != nullptr)
        publish (nullptr, {}, {});
}

void WavetableImporter::writeTo (juce::ValueTree& state) const
{
    const juce::ScopedLock sl (lock);

    if (table == nullptr)
        return;

    // Each sample as a 32-bit float, little-endian whatever the computer, so a project moves between them.
    juce::MemoryBlock data (cycles.size() * (size_t) WaveTable::size * sizeof (juce::uint32));
    auto* bytes = static_cast<juce::uint32*> (data.getData());

    for (const auto& cycle : cycles)
    {
        for (auto sample : cycle)
        {
            juce::uint32 bits;
            std::memcpy (&bits, &sample, sizeof (bits));
            *bytes++ = juce::ByteOrder::swapIfBigEndian (bits);
        }
    }

    juce::ValueTree saved (stateChildId);
    saved.setProperty ("name", status.fileName, nullptr);
    saved.setProperty ("frames", (int) cycles.size(), nullptr);
    saved.setProperty ("data", juce::Base64::toBase64 (data.getData(), data.getSize()), nullptr);
    state.appendChild (saved, nullptr);
}

void WavetableImporter::readFrom (const juce::ValueTree& state, IfNoneSaved ifNoneSaved)
{
    const auto saved = state.getChildWithName (stateChildId);

    if (! saved.isValid())
    {
        if (ifNoneSaved == IfNoneSaved::clear)
            clear();

        return;
    }

    const auto name = saved.getProperty ("name").toString();
    const auto numFrames = (int) saved.getProperty ("frames", 0);
    // Plain base64, which says nothing about its own length: so a copy cut short comes out short, and
    // is refused below.
    juce::MemoryOutputStream decodedData;
    const auto decoded = juce::Base64::convertFromBase64 (decodedData, saved.getProperty ("data").toString());
    const auto data = decodedData.getMemoryBlock();

    if (! decoded || numFrames < 1 || numFrames > maxFrames || data.getSize() != (size_t) numFrames * WaveTable::size * sizeof (juce::uint32))
    {
        if (getTable() != nullptr)
            publish (nullptr, {}, { {}, name + ": the wavetable saved here couldn't be read" });
        else
        {
            const juce::ScopedLock sl (lock);
            status = { {}, name + ": the wavetable saved here couldn't be read" };
        }

        return;
    }

    std::vector<std::vector<float>> frames ((size_t) numFrames, std::vector<float> ((size_t) WaveTable::size));
    const auto* bytes = static_cast<const juce::uint32*> (data.getData());

    for (auto& frame : frames)
    {
        for (auto& sample : frame)
        {
            const auto bits = juce::ByteOrder::swapIfBigEndian (*bytes++);
            std::memcpy (&sample, &bits, sizeof (sample));

            if (! std::isfinite (sample))
                sample = 0.0f;
        }
    }

    // Built here, not on the thread, so it's ready before this returns.
    auto newTable = std::make_unique<MultiFrameWaveTable> (frames);
    publish (std::move (newTable), std::move (frames), { name, {} });
}

void WavetableImporter::freeOldTables()
{
    const juce::ScopedLock sl (lock);
    const auto blocks = blocksRendered.load();
    const auto samples = samplesRendered.load();

    oldTables.erase (std::remove_if (oldTables.begin(), oldTables.end(), [=] (const OldTable& old)
    {
        return blocks >= old.blocksWhenReplaced + blocksBeforeFreeing && samples >= old.samplesWhenReplaced + samplesBeforeFreeing;
    }), oldTables.end());
}

WavetableImporter::Cycles WavetableImporter::readCycles (const juce::File& file)
{
    if (! file.existsAsFile())
        return { {}, "the file couldn't be found" };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return { {}, "it isn't an audio file that can be read" };

    const auto length = reader->lengthInSamples;

    if (length <= 0 || reader->numChannels <= 0)
        return { {}, "it's empty" };

    const auto isWholeFrames = length % WaveTable::size == 0;

    if (isWholeFrames && length / WaveTable::size > maxFrames)
        return { {}, "it has more than " + juce::String (maxFrames) + " frames" };

    if (! isWholeFrames && length > maxSingleCycleSamples)
        return { {}, "it isn't a whole number of 2048-sample frames" };

    const auto samples = readSamples (*reader);
    std::vector<std::vector<float>> frames;

    if (isWholeFrames)
    {
        for (size_t start = 0; start < samples.size(); start += (size_t) WaveTable::size)
            frames.emplace_back (samples.begin() + (std::ptrdiff_t) start, samples.begin() + (std::ptrdiff_t) start + WaveTable::size);
    }
    else
    {
        frames.push_back (resampleCycle (samples));
    }

    for (auto& frame : frames)
        matchFrame (frame);

    return { std::move (frames), {} };
}

} // namespace boringsynth
