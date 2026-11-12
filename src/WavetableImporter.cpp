#include "WavetableImporter.h"

namespace boringsynth
{

namespace
{

// While old tables are kept, how often to check whether they can be freed, in milliseconds. Only how soon
// memory comes back depends on it; whether it's safe doesn't.
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

ImportedTable::ImportedTable (std::vector<std::vector<float>> frames, const juce::String& fileName, int number)
    : cycles (std::move (frames)),
      table (cycles.empty() ? nullptr : std::make_unique<MultiFrameWaveTable> (cycles)),
      name (fileName),
      generation (number)
{
}

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

ImportedTable::Ptr WavetableImporter::getLatest() const
{
    const juce::ScopedLock sl (lock);
    return tables.empty() ? nullptr : tables.back();
}

const MultiFrameWaveTable* WavetableImporter::getTable() const
{
    const juce::ScopedLock sl (lock);
    return tables.empty() ? nullptr : tables.back()->getTable();
}

void WavetableImporter::prepareForBlock() noexcept
{
    // Taking the reference here is safe: the importer won't free a table the audio thread hasn't moved
    // past, and letting go of the old one isn't the last reference, as the importer still holds it.
    if (auto* newest = waiting.exchange (nullptr))
    {
        audioTable = newest;
        takenByAudio.store (newest->getGeneration());
    }
}

int WavetableImporter::getNumOldTables() const
{
    const juce::ScopedLock sl (lock);
    return std::max (0, (int) tables.size() - 1);
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

        freeUnusedTables();

        const auto anyWaiting = [this]
        {
            const juce::ScopedLock sl (lock);

            if (pendingFile == juce::File())
                busy = false;

            return tables.size() > 1;
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

    publish (std::move (read.frames), { file.getFileName(), {} });
}

void WavetableImporter::publish (std::vector<std::vector<float>> frames, const Status& newStatus)
{
    // One at a time, so tables are numbered, and reach the audio thread, in the order they're made: a file
    // can finish loading on the thread as a project loads on the message thread.
    const juce::ScopedLock oneAtATime (publishLock);

    // Built before the other lock, as it takes a while, and the window reads under that one.
    ImportedTable::Ptr newest = new ImportedTable (std::move (frames), newStatus.fileName, generation.load() + 1);

    {
        const juce::ScopedLock sl (lock);
        tables.push_back (newest);
        status = newStatus;
        generation = newest->getGeneration();
        waiting.store (newest.get());
    }

    // The thread frees old tables, once nothing's using them.
    if (! isThreadRunning())
        startThread();

    notify();
}

void WavetableImporter::clear()
{
    if (getTable() != nullptr)
        publish ({}, {});
}

void WavetableImporter::writeTo (juce::ValueTree& state) const
{
    const auto latest = getLatest();

    if (latest == nullptr || latest->getTable() == nullptr)
        return;

    const auto& cycles = latest->getCycles();

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
    saved.setProperty ("name", latest->getFileName(), nullptr);
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
            publish ({}, { {}, name + ": the wavetable saved here couldn't be read" });
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
    publish (std::move (frames), { name, {} });
}

// An old table can go once only the importer holds it, and the audio thread has picked up a newer one: so
// it isn't waiting to be picked up, and the audio thread can't take a new reference to it. The newest is
// always kept.
void WavetableImporter::freeUnusedTables()
{
    const juce::ScopedLock sl (lock);
    const auto audioHasMovedPast = takenByAudio.load();

    for (size_t i = 0; i + 1 < tables.size();)
    {
        if (tables[i]->getReferenceCount() == 1 && tables[i]->getGeneration() < audioHasMovedPast)
            tables.erase (tables.begin() + (std::ptrdiff_t) i);
        else
            ++i;
    }
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
