#include "TestHelpers.h"

#include "BoringSynthEditor.h"
#include "BoringSynthProcessor.h"
#include "WavetableDisplay.h"
#include "WavetableImporter.h"

// Import tests: reading wavetables from files, the Imported waveform playing through the whole plug-in,
// replacing a table while notes play, and loading one from the window.

namespace testing
{

namespace
{

using boringsynth::BoringSynthProcessor;
using boringsynth::WavetableImporter;

constexpr int frameSize = boringsynth::WaveTable::size;
constexpr int imported = boringsynth::importedWaveform;

const auto twoPi = juce::MathConstants<double>::twoPi;

// A frame of a shape, given the phase.
std::vector<float> makeFrame (const std::function<double (double phase)>& shape, int numSamples = frameSize)
{
    std::vector<float> frame ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
        frame[(size_t) i] = (float) shape (twoPi * i / numSamples);

    return frame;
}

const auto sineShape = [] (double phase) { return std::sin (phase); };
const auto thirdHarmonicShape = [] (double phase) { return 0.5 * std::sin (3.0 * phase); };
const auto cosineShape = [] (double phase) { return std::cos (phase); };

// A WAV file of these samples, one list for each channel, deleted when this goes.
struct WavFile
{
    juce::TemporaryFile temporary { ".wav" };

    explicit WavFile (const std::vector<std::vector<float>>& channels)
    {
        juce::AudioBuffer<float> buffer ((int) channels.size(), (int) channels[0].size());

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.copyFrom (channel, 0, channels[(size_t) channel].data(), buffer.getNumSamples());

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (buffer.getNumChannels()).withBitsPerSample (32)
                                                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    juce::File file() const { return temporary.getFile(); }
};

// One channel of these frames, one after another.
std::vector<float> joined (const std::vector<std::vector<float>>& frames)
{
    std::vector<float> samples;

    for (const auto& frame : frames)
        samples.insert (samples.end(), frame.begin(), frame.end());

    return samples;
}

void waitForImport (WavetableImporter& importer)
{
    for (int waited = 0; waited < 30000 && importer.isBusy(); waited += 2)
        juce::Thread::sleep (2);
}

BoringSynthProcessor& asSynth (juce::AudioProcessor& processor)
{
    return dynamic_cast<BoringSynthProcessor&> (processor);
}

void importAndWait (juce::AudioProcessor& processor, const juce::File& file)
{
    asSynth (processor).getWavetableImporter().import (file);
    waitForImport (asSynth (processor).getWavetableImporter());
}

double largestDifferenceFrom (const std::vector<float>& frame, const std::function<double (double phase)>& shape)
{
    double largest = 0.0;

    for (int i = 0; i < (int) frame.size(); ++i)
        largest = std::max (largest, std::abs ((double) frame[(size_t) i] - shape (twoPi * i / (double) frame.size())));

    return largest;
}

double peakOf (const std::vector<float>& frame)
{
    double peak = 0.0;

    for (auto sample : frame)
        peak = std::max (peak, (double) std::abs (sample));

    return peak;
}

//==============================================================================
void testReadingFrames()
{
    const auto sine = makeFrame (sineShape), third = makeFrame (thirdHarmonicShape);
    const WavFile file ({ joined ({ sine, third, sine }) });
    const auto cycles = WavetableImporter::readCycles (file.file());

    check (cycles.error.isEmpty() && cycles.frames.size() == 3, "a whole number of 2048-sample frames is split into those frames");

    if (cycles.frames.size() != 3)
        return;

    // Each is scaled to the sine's RMS, so the half-level third harmonic comes out at full level.
    const auto sineError = largestDifferenceFrom (cycles.frames[0], sineShape);
    const auto thirdError = largestDifferenceFrom (cycles.frames[1], [] (double phase) { return std::sin (3.0 * phase); });
    std::printf ("      frames from a file: sine within %.2e, scaled third harmonic within %.2e\n", sineError, thirdError);
    check (sineError < 1.0e-5 && thirdError < 1.0e-5, "each frame holds its part of the file, in order, scaled to the sine's loudness");
}

void testChannelsAreMixed()
{
    // A sine on the left, and the same sine plus a third harmonic and minus it on either side mixes to the sine.
    const auto left = makeFrame ([] (double phase) { return std::sin (phase) + std::sin (3.0 * phase); });
    const auto right = makeFrame ([] (double phase) { return std::sin (phase) - std::sin (3.0 * phase); });
    const WavFile file ({ left, right });
    const auto cycles = WavetableImporter::readCycles (file.file());
    const auto error = cycles.frames.empty() ? 1.0 : largestDifferenceFrom (cycles.frames[0], sineShape);
    check (error < 1.0e-5, "a file's channels are mixed to one");
}

void testDcOffsetRemoved()
{
    const WavFile file ({ makeFrame ([] (double phase) { return 0.4 + std::sin (phase); }) });
    const auto cycles = WavetableImporter::readCycles (file.file());
    const auto error = cycles.frames.empty() ? 1.0 : largestDifferenceFrom (cycles.frames[0], sineShape);
    std::printf ("      a sine with a DC offset: within %.2e of a sine\n", error);
    check (error < 1.0e-5, "a frame's DC offset is removed");
}

void testSingleCycle()
{
    // A sine 600 samples long, resampled along straight lines.
    const WavFile file ({ makeFrame (sineShape, 600) });
    const auto cycles = WavetableImporter::readCycles (file.file());
    const auto right = cycles.frames.size() == 1 && (int) cycles.frames[0].size() == frameSize;
    const auto error = right ? largestDifferenceFrom (cycles.frames[0], sineShape) : 1.0;
    std::printf ("      a 600-sample sine resampled to 2048: within %.2e of a sine\n", error);
    check (right && error < 1.0e-4, "a file of another length, up to 8192 samples, is one cycle, resampled");
}

void testLoudnessLimits()
{
    const auto quiet = makeFrame ([] (double phase) { return 0.001 * std::sin (phase); });
    auto spike = std::vector<float> ((size_t) frameSize, 0.0f);
    spike[100] = 1.0f;
    const auto silent = std::vector<float> ((size_t) frameSize, 0.0f);
    auto broken = makeFrame (sineShape);
    broken[50] = std::numeric_limits<float>::quiet_NaN();
    broken[51] = std::numeric_limits<float>::infinity();

    const WavFile file ({ joined ({ quiet, spike, silent, broken }) });
    const auto cycles = WavetableImporter::readCycles (file.file());

    if (cycles.frames.size() != 4)
    {
        check (false, "a file with quiet, spiky, silent and broken frames loads");
        return;
    }

    bool allFinite = true;

    for (const auto& frame : cycles.frames)
        for (auto sample : frame)
            allFinite = allFinite && std::isfinite (sample);

    std::printf ("      peaks: quiet %.3f, spike %.3f, silent %.3f, broken %.3f\n", peakOf (cycles.frames[0]), peakOf (cycles.frames[1]),
                 peakOf (cycles.frames[2]), peakOf (cycles.frames[3]));
    check (std::abs (peakOf (cycles.frames[0]) - 1.0) < 1.0e-4, "a quiet frame is brought up to the sine's loudness");
    check (peakOf (cycles.frames[1]) <= 3.0 + 1.0e-5 && peakOf (cycles.frames[1]) > 2.9, "a spiky frame's peak stops at 3 times the sine's");
    check (peakOf (cycles.frames[2]) == 0.0, "a silent frame stays silent");
    check (allFinite, "anything in a file that isn't a number becomes silence");
}

void testRefusedFiles()
{
    const auto refusedBecause = [] (const juce::File& file) { return WavetableImporter::readCycles (file); };

    const WavFile notWhole ({ std::vector<float> (10000, 0.1f) });
    const auto tooLong = refusedBecause (notWhole.file());
    check (tooLong.frames.empty() && tooLong.error.contains ("2048"), "a long file that isn't whole frames is refused, saying why");

    const WavFile tooMany ({ std::vector<float> ((size_t) frameSize * 257, 0.1f) });
    const auto tooManyFrames = refusedBecause (tooMany.file());
    check (tooManyFrames.frames.empty() && tooManyFrames.error.contains ("256"), "a file of more than 256 frames is refused, saying why");

    const WavFile mostFrames ({ std::vector<float> ((size_t) frameSize * 256, 0.1f) });
    check (refusedBecause (mostFrames.file()).frames.size() == 256, "a file of exactly 256 frames loads");

    const juce::TemporaryFile text (".wav");
    text.getFile().replaceWithText ("this isn't audio");
    check (! refusedBecause (text.getFile()).error.isEmpty(), "a file that isn't audio is refused, saying why");

    check (! refusedBecause (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no such wavetable.wav")).error.isEmpty(),
           "a missing file is refused, saying why");
}

void testFailedImportKeepsTheTable()
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    auto& importer = asSynth (*processor).getWavetableImporter();
    const WavFile good ({ makeFrame (sineShape) });
    importAndWait (*processor, good.file());
    const auto* table = importer.getTable();
    const auto generation = importer.getGeneration();

    const juce::TemporaryFile text (".wav");
    text.getFile().replaceWithText ("this isn't audio");
    importAndWait (*processor, text.getFile());
    const auto status = importer.getStatus();

    std::printf ("      after a failed import: '%s', file '%s'\n", status.error.toRawUTF8(), status.fileName.toRawUTF8());
    check (importer.getTable() == table && importer.getGeneration() == generation, "a failed import leaves the table as it was");
    check (status.error.contains (text.getFile().getFileName()) && status.fileName == good.file().getFileName(), "and says why, still naming the file loaded");

    importAndWait (*processor, good.file());
    check (importer.getStatus().error.isEmpty(), "a later import that works clears the error");
}

//==============================================================================
void testImportedPlays()
{
    // With nothing loaded, Imported is a plain sine.
    const auto chooseImported = [] (juce::AudioProcessor& p) { setParameter (p, "waveform", (float) imported); };
    const auto sineNote = render ({ .events = heldNote });
    check (render ({ .events = heldNote, .setUp = chooseImported }) == sineNote, "with nothing loaded, Imported plays a sine");

    // A file of a sine plays a sine.
    const WavFile sineFile ({ makeFrame (sineShape) });
    const auto difference = largestDifference (render ({ .events = heldNote,
                                                         .setUp = [&] (juce::AudioProcessor& p)
                                                         {
                                                             chooseImported (p);
                                                             importAndWait (p, sineFile.file());
                                                         } }),
                                               sineNote);
    std::printf ("      a file of a sine against the Sine waveform: largest difference %.2e\n", difference);
    check (difference < 1.0e-5, "a loaded table plays through the plug-in");

    // Position moves through its frames, here half way from the sine to a third harmonic.
    const WavFile twoFrames ({ joined ({ makeFrame (sineShape), makeFrame ([] (double phase) { return std::sin (3.0 * phase); }) }) });
    const auto midway = render ({ .events = heldNote,
                                  .setUp = [&] (juce::AudioProcessor& p)
                                  {
                                      chooseImported (p);
                                      setParameter (p, "wavetablePosition", 50.0f);
                                      importAndWait (p, twoFrames.file());
                                  } });
    const auto phaseDelta = twoPi * 440.0 / testSampleRate;
    double midwayError = 0.0;

    for (int i = 0; i < (int) midway.size(); ++i)
        midwayError = std::max (midwayError, std::abs ((double) midway[(size_t) i] - 0.1 * 0.5 * (std::sin (phaseDelta * i) + std::sin (3.0 * phaseDelta * i))));

    std::printf ("      midway between a file's two frames: largest difference %.2e\n", midwayError);
    check (midwayError < 1.0e-5, "Position moves through a loaded table's frames");

    // Other waveforms are as they were.
    const auto sawAlone = render ({ .events = heldNote, .setUp = [] (juce::AudioProcessor& p) { setParameter (p, "waveform", 2.0f); } });
    const auto sawWithLoaded = render ({ .events = heldNote,
                                         .setUp = [&] (juce::AudioProcessor& p)
                                         {
                                             setParameter (p, "waveform", 2.0f);
                                             importAndWait (p, sineFile.file());
                                         } });
    check (sawAlone == sawWithLoaded, "a loaded table makes no difference to the other waveforms");
}

void testStartsAtZero()
{
    const juce::SharedResourcePointer<boringsynth::WaveformTables> tables;
    bool builtInsDo = true;

    for (int waveform = 0; waveform < imported; ++waveform)
        builtInsDo = builtInsDo && tables->get (waveform).startsAtZero();

    check (builtInsDo, "every built-in waveform starts at zero");

    const boringsynth::MultiFrameWaveTable sines (std::vector<std::vector<float>> { makeFrame (sineShape), makeFrame (sineShape) });
    const boringsynth::MultiFrameWaveTable lastDoesnt (std::vector<std::vector<float>> { makeFrame (sineShape), makeFrame (cosineShape) });
    check (sines.startsAtZero() && ! lastDoesnt.startsAtZero(), "a table knows whether all its frames do");
}

void testNoteFadesIn()
{
    // A cosine starts at its peak. Starting there at once would jump from silence to full level; the note
    // should fade in over 5 ms (240 samples), then be exactly the wave.
    const WavFile file ({ makeFrame (cosineShape) });
    const auto setUp = [&] (juce::AudioProcessor& p)
    {
        setParameter (p, "waveform", (float) imported);
        importAndWait (p, file.file());
    };

    const auto output = render ({ .numSamples = 4800, .events = heldNote, .setUp = setUp });
    const auto phaseDelta = twoPi * 440.0 / testSampleRate;
    double fadeError = 0.0, afterError = 0.0;

    for (int i = 0; i < (int) output.size(); ++i)
    {
        // A cosine scaled to the sine's RMS is the same level.
        const auto wave = 0.1 * std::cos (phaseDelta * i);
        const auto expected = wave * std::min (1.0, i / 240.0);
        (i < 240 ? fadeError : afterError) = std::max (i < 240 ? fadeError : afterError, std::abs ((double) output[(size_t) i] - expected));
    }

    const auto startStep = largestStep (std::vector<float> (output.begin(), output.begin() + 300));
    const auto steadyStep = largestStep (std::vector<float> (output.begin() + 1000, output.begin() + 2000));
    std::printf ("      a note on a cosine: first sample %.5f, largest step %.5f at the start, %.5f steady\n", output[0], startStep, steadyStep);
    check (output[0] == 0.0f && startStep <= steadyStep * 1.2f, "a note on a table that doesn't start at zero doesn't click");
    check (fadeError < 1.0e-4 && afterError < 1.0e-4, "it fades in over 5 ms, then plays the wave as it is");

    // And it's the same in any size of block.
    const auto in512 = render ({ .numSamples = 4800, .events = heldNote, .setUp = setUp });
    const auto in7 = render ({ .blockSize = 7, .numSamples = 4800, .events = heldNote, .setUp = setUp });
    check (in7 == in512, "the fade doesn't depend on the host's block size");

    // A note stolen part-way through its fade-in should carry on from where it had got to, not jump to
    // full level. Sixteen notes, then a seventeenth during their fades, against the same without it. At a
    // few moments, as a jump is hidden when the stolen note's wave happens to be crossing zero.
    std::vector<TimedMessage> events;

    for (int i = 0; i < 16; ++i)
        events.push_back ({ 0, noteOn (40 + i) });

    const auto quieter = [&] (juce::AudioProcessor& p)
    {
        setUp (p);
        setParameter (p, "volume", -12.0f);
    };
    const auto withoutSteal = render ({ .numSamples = 960, .events = events, .setUp = quieter });
    double worstStep = 0.0, worstUnstolen = 0.0;

    for (auto stealAt : { 30, 60, 100, 150 })
    {
        auto stealing = events;
        stealing.push_back ({ stealAt, noteOn (70) });
        const auto withSteal = render ({ .numSamples = 960, .events = stealing, .setUp = quieter });
        const auto stepAround = [stealAt] (const std::vector<float>& output)
        {
            return (double) largestStep (std::vector<float> (output.begin() + stealAt - 5, output.begin() + stealAt + 5));
        };

        worstStep = std::max (worstStep, stepAround (withSteal));
        worstUnstolen = std::max (worstUnstolen, stepAround (withoutSteal));
    }

    std::printf ("      stolen during its fade-in: largest step %.5f, without the steal %.5f\n", worstStep, worstUnstolen);
    check (worstStep <= worstUnstolen * 1.5, "a note stolen during its fade-in doesn't jump");
}

//==============================================================================
void testReplacingATableCrossfades()
{
    // A new table arriving during a held note: exactly the old table before it, an even blend halfway
    // through the 10 ms crossfade, then exactly the new one.
    const WavFile first ({ makeFrame (sineShape) });
    const WavFile second ({ makeFrame ([] (double phase) { return std::sin (3.0 * phase); }) });
    const auto withFile = [] (const juce::File& file)
    {
        return [file] (juce::AudioProcessor& p)
        {
            setParameter (p, "waveform", (float) imported);
            importAndWait (p, file);
        };
    };

    const auto onlyFirst = render ({ .events = heldNote, .setUp = withFile (first.file()) });
    const auto onlySecond = render ({ .events = heldNote, .setUp = withFile (second.file()) });
    const auto replaced = render ({ .events = heldNote,
                                    .setUp = withFile (first.file()),
                                    .beforeEachBlock = [&] (juce::AudioProcessor& p, int blockStart)
                                    {
                                        if (blockStart == changeSample)
                                            importAndWait (p, second.file());
                                    } });

    constexpr int fadeLength = 480;
    const auto halfway = (size_t) (changeSample + fadeLength / 2 - 1);
    const auto blend = 0.5f * (onlyFirst[halfway] + onlySecond[halfway]);

    check (std::equal (replaced.begin(), replaced.begin() + changeSample, onlyFirst.begin()), "before a new table arrives, the note is unchanged");
    check (std::abs (replaced[halfway] - blend) < 1.0e-6f, "halfway through the 10 ms crossfade, the note is an even blend of both tables");
    check (std::equal (replaced.begin() + changeSample + fadeLength - 1, replaced.end(), onlySecond.begin() + changeSample + fadeLength - 1),
           "after the crossfade, the note is exactly the new table");
}

void waitForOldTables (const WavetableImporter& importer, int expected)
{
    for (int waited = 0; waited < 3000 && importer.getNumOldTables() != expected; waited += 10)
        juce::Thread::sleep (10);
}

void testOldTablesKeptWhileInUse()
{
    // A replaced table is kept for as long as anything might read it, however long that is: here, a note
    // held on it, and the audio not having picked up the newer one. Once neither holds, it's freed.
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    auto& importer = asSynth (*processor).getWavetableImporter();
    setParameter (*processor, "waveform", (float) imported);
    setParameter (*processor, "ampRelease", 0.01f);
    processor->prepareToPlay (testSampleRate, 512);

    juce::AudioBuffer<float> buffer (1, 512);
    const auto play = [&] (int numBlocks, std::optional<juce::MidiMessage> message = std::nullopt)
    {
        for (int block = 0; block < numBlocks; ++block)
        {
            juce::MidiBuffer midi;

            if (block == 0 && message.has_value())
                midi.addEvent (*message, 0);

            buffer.clear();
            processor->processBlock (buffer, midi);
        }
    };

    const WavFile first ({ makeFrame (sineShape) }), second ({ makeFrame (thirdHarmonicShape) }), third ({ makeFrame (cosineShape) });
    importAndWait (*processor, first.file());
    play (4, noteOn (60));

    // The note fades to the new table, and holds on to the old one until the note's over: a minute of
    // held note couldn't be safer than this.
    importAndWait (*processor, second.file());
    play (200);
    juce::Thread::sleep (500);
    check (importer.getNumOldTables() == 1, "a replaced table is kept while a note that played it is still sounding");

    play (10, noteOff (60));
    waitForOldTables (importer, 0);
    check (importer.getNumOldTables() == 0, "once the note's over, it's freed");

    // With no audio running, the newer table isn't picked up, so the one the audio has is kept.
    importAndWait (*processor, third.file());
    juce::Thread::sleep (500);
    check (importer.getNumOldTables() == 1, "with no audio running, the table the audio last had is kept");

    play (1);
    waitForOldTables (importer, 0);
    check (importer.getNumOldTables() == 0, "once the audio has the newer one, the old one's freed");
}

void testStolenNoteKeepsItsTable()
{
    // A note on the old table, crossfading to a new one, is stolen: its fade-out still reads the old table,
    // which nothing else holds by then. The audio pauses, long enough for the importer to free whatever it
    // can, before the fade-out goes on. Were the old table freed, the fade would read freed memory.
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    auto& importer = asSynth (*processor).getWavetableImporter();
    setParameter (*processor, "waveform", (float) imported);
    processor->prepareToPlay (testSampleRate, 64);

    const WavFile first ({ makeFrame (sineShape) }), second ({ makeFrame (thirdHarmonicShape) });
    importAndWait (*processor, first.file());

    juce::AudioBuffer<float> buffer (1, 64);
    const auto play = [&] (juce::MidiBuffer midi)
    {
        buffer.clear();
        processor->processBlock (buffer, midi);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (0, i)) || std::abs (buffer.getSample (0, i)) > 1.0f)
                return false;

        return true;
    };

    // One note on the old table, in the middle of the range, so it's the one stolen.
    juce::MidiBuffer midi;
    midi.addEvent (noteOn (60), 0);
    bool clean = play (midi);

    // The new table arrives; in the same block, fifteen notes take the other voices, and a seventeenth
    // steals the first while it's fading from the old table to the new one.
    importAndWait (*processor, second.file());
    midi.clear();

    for (int i = 0; i < 15; ++i)
        midi.addEvent (noteOn (i < 8 ? 40 + i : 70 + i), 1);

    midi.addEvent (noteOn (62), 2);
    clean = play (midi) && clean;

    for (int block = 0; block < 10; ++block)
    {
        juce::Thread::sleep (block == 0 ? 600 : 0);
        clean = play ({}) && clean;
    }

    check (clean, "a note stolen while fading from a replaced table keeps that table until it's faded out");

    // Once it has, nothing holds the old table.
    waitForOldTables (importer, 0);
    check (importer.getNumOldTables() == 0, "and the old table's freed afterwards");
}

void testReplacingTablesWhilePlaying()
{
    // The real thing, as hard as it can be: notes being stolen and released, on an imported table, with
    // the waveform moving on and off it, while new tables keep arriving, each on its own thread as the audio
    // runs. A table read after it was freed would show up as a crash or nonsense.
    const WavFile first ({ joined ({ makeFrame (sineShape), makeFrame (cosineShape) }) });
    const WavFile second ({ makeFrame ([] (double phase) { return std::sin (3.0 * phase); }) });

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    auto& importer = asSynth (*processor).getWavetableImporter();
    setParameter (*processor, "waveform", (float) imported);
    setParameter (*processor, "wavetablePosition", 50.0f);
    setParameter (*processor, "ampRelease", 0.02f);
    processor->prepareToPlay (testSampleRate, 64);

    juce::AudioBuffer<float> buffer (2, 64);
    juce::Random random (29);
    bool allFinite = true;
    auto largest = 0.0f;
    int blocks = 0;
    const auto generationAtStart = importer.getGeneration();

    for (int round = 0; round < 40; ++round)
    {
        importer.import (round % 2 == 0 ? first.file() : second.file());

        // Keep the audio running for as long as the table takes to arrive, and a little after.
        for (int after = 0; after < 30; after = importer.isBusy() ? 0 : after + 1)
        {
            juce::MidiBuffer midi;

            for (int i = 0; i < 4; ++i)
                midi.addEvent (random.nextBool() ? noteOn (40 + random.nextInt (40)) : noteOff (40 + random.nextInt (40)), random.nextInt (64));

            if (random.nextInt (16) == 0)
                setParameter (*processor, "waveform", random.nextBool() ? (float) imported : 2.0f);

            buffer.clear();
            processor->processBlock (buffer, midi);
            ++blocks;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                allFinite = allFinite && std::isfinite (buffer.getSample (0, i));
                largest = std::max (largest, std::abs (buffer.getSample (0, i)));
            }
        }
    }

    std::printf ("      %d tables replaced over %d blocks while playing, loudest sample %.3f\n",
                 importer.getGeneration() - generationAtStart, blocks, largest);
    check (importer.getGeneration() - generationAtStart == 40, "every table arrived");
    check (allFinite && largest <= 1.0f, "replacing tables while notes are being stolen and released stays clean");

    // Once every note's over, everything old can go.
    juce::MidiBuffer allOff;
    allOff.addEvent (controller (123, 0), 0);

    for (int block = 0; block < 200; ++block)
    {
        buffer.clear();
        processor->processBlock (buffer, allOff);
        allOff.clear();
    }

    waitForOldTables (importer, 0);
    check (importer.getNumOldTables() == 0, "and every old table is freed once nothing's playing");
}

void testEveryCopyHasItsOwn()
{
    std::unique_ptr<juce::AudioProcessor> one (createPluginFilter()), other (createPluginFilter());
    const WavFile file ({ makeFrame (sineShape) });
    importAndWait (*one, file.file());
    check (asSynth (*one).getWavetableImporter().getTable() != nullptr && asSynth (*other).getWavetableImporter().getTable() == nullptr,
           "each copy of the plug-in loads its own wavetable");
}

//==============================================================================
// Saving with projects and presets.

std::unique_ptr<juce::AudioProcessor> reopened (juce::AudioProcessor& from, juce::MemoryBlock* stateOut = nullptr)
{
    juce::MemoryBlock state;
    from.getStateInformation (state);
    std::unique_ptr<juce::AudioProcessor> to (createPluginFilter());
    to->setStateInformation (state.getData(), (int) state.getSize());

    if (stateOut != nullptr)
        *stateOut = state;

    return to;
}

std::vector<float> renderHeldNote (juce::AudioProcessor& processor)
{
    processor.prepareToPlay (testSampleRate, 512);
    juce::AudioBuffer<float> buffer (1, 512);
    std::vector<float> output;

    for (int block = 0; block < 40; ++block)
    {
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent (noteOn (57), 0);

        buffer.clear();
        processor.processBlock (buffer, midi);
        output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
    }

    return output;
}

void testSavedWithTheProject()
{
    std::unique_ptr<juce::AudioProcessor> original (createPluginFilter());
    juce::MemoryBlock state;
    std::unique_ptr<juce::AudioProcessor> restored;
    juce::String fileName;

    {
        // The file is deleted before the project's opened again: the table is in the project.
        const WavFile file ({ joined ({ makeFrame (sineShape), makeFrame (cosineShape), makeFrame (thirdHarmonicShape) }) });
        fileName = file.file().getFileName();
        setParameter (*original, "waveform", (float) imported);
        setParameter (*original, "wavetablePosition", 40.0f);
        importAndWait (*original, file.file());
    }

    restored = reopened (*original, &state);
    auto& importer = asSynth (*restored).getWavetableImporter();
    std::printf ("      a project with a 3-frame table: %d bytes\n", (int) state.getSize());
    check (importer.getTable() != nullptr && importer.getTable()->getNumFrames() == 3 && importer.getStatus().fileName == fileName,
           "a project reopens with its imported wavetable, named, though the file's gone");
    check (! importer.isBusy(), "it's ready as soon as the project's loaded, before any audio");
    check (renderHeldNote (*original) == renderHeldNote (*restored), "and sounds exactly as it did");

    // The table isn't left among the parameters, where it would be saved twice.
    juce::MemoryBlock again;
    restored->getStateInformation (again);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
    int saved = 0;

    if (xml != nullptr)
        for (auto* child : xml->getChildIterator())
            saved += child->hasTagName (WavetableImporter::stateChildId) ? 1 : 0;

    check (saved == 1 && std::abs ((int) again.getSize() - (int) state.getSize()) < 100, "saved again, it's saved once, the same size");
}

void testProjectWithoutATable()
{
    // A project saved with no table, as every one before this was, clears whatever's loaded.
    std::unique_ptr<juce::AudioProcessor> withTable (createPluginFilter()), without (createPluginFilter());
    const WavFile file ({ makeFrame (thirdHarmonicShape) });
    importAndWait (*withTable, file.file());

    juce::MemoryBlock state;
    without->getStateInformation (state);
    withTable->setStateInformation (state.getData(), (int) state.getSize());
    check (asSynth (*withTable).getWavetableImporter().getTable() == nullptr, "a project without an imported wavetable unloads the one loaded");
}

void testDamagedSavedTable()
{
    // A saved table that's been cut short: the project still opens, its parameters loaded, with no
    // table and a reason why.
    std::unique_ptr<juce::AudioProcessor> original (createPluginFilter());
    const WavFile file ({ joined ({ makeFrame (sineShape), makeFrame (cosineShape) }) });
    importAndWait (*original, file.file());
    setParameter (*original, "volume", -7.0f);

    juce::MemoryBlock state;
    original->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    auto* saved = xml != nullptr ? xml->getChildByName (WavetableImporter::stateChildId) : nullptr;

    if (saved == nullptr)
    {
        check (false, "a saved project holds its imported wavetable");
        return;
    }

    for (auto damage : { 0, 1 })
    {
        auto damaged = std::make_unique<juce::XmlElement> (*xml);
        auto* table = damaged->getChildByName (WavetableImporter::stateChildId);

        if (damage == 0)
        {
            table->setAttribute ("data", table->getStringAttribute ("data").dropLastCharacters (400));
        }
        else
        {
            // Consistent, but past the limit: a file couldn't have given this.
            const juce::MemoryBlock tooMany ((size_t) 257 * frameSize * sizeof (float), true);
            table->setAttribute ("frames", 257);
            table->setAttribute ("data", juce::Base64::toBase64 (tooMany.getData(), tooMany.getSize()));
        }

        juce::MemoryBlock damagedState;
        juce::AudioProcessor::copyXmlToBinary (*damaged, damagedState);
        std::unique_ptr<juce::AudioProcessor> restored (createPluginFilter());
        const WavFile other ({ makeFrame (thirdHarmonicShape) });
        importAndWait (*restored, other.file());
        restored->setStateInformation (damagedState.getData(), (int) damagedState.getSize());

        const auto status = asSynth (*restored).getWavetableImporter().getStatus();
        std::printf ("      a damaged saved table: '%s'\n", status.error.toRawUTF8());
        check (asSynth (*restored).getWavetableImporter().getTable() == nullptr && status.error.contains ("couldn't be read")
                   && std::abs (getParameter (*restored, "volume") + 7.0f) < 0.01f,
               damage == 0 ? "a saved table cut short is refused, saying why, and the rest of the project loads"
                           : "a saved table claiming too many frames is refused, saying why, and the rest of the project loads");
    }
}

void testSavedInPresets()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BoringSynth import preset test");
    folder.deleteRecursively();

    std::unique_ptr<juce::AudioProcessor> saver (createPluginFilter()), loader (createPluginFilter());

    for (auto* processor : { saver.get(), loader.get() })
        asSynth (*processor).getPresetManager().setUserPresetFolder (folder);

    // A preset saved with a table brings it with it.
    {
        const WavFile file ({ joined ({ makeFrame (sineShape), makeFrame (thirdHarmonicShape) }) });
        importAndWait (*saver, file.file());
        setParameter (*saver, "waveform", (float) imported);
        asSynth (*saver).getPresetManager().saveUserPreset ("With a table");
    }

    asSynth (*loader).getPresetManager().loadUserPreset ("With a table");
    const auto* loaded = asSynth (*loader).getWavetableImporter().getTable();
    check (loaded != nullptr && loaded->getNumFrames() == 2 && getParameter (*loader, "waveform") == (float) imported,
           "a preset of your own brings its imported wavetable with it");
    check (renderHeldNote (*saver) == renderHeldNote (*loader), "and sounds exactly as it did");

    // One saved without a table, or a factory preset, leaves the loaded table alone.
    std::unique_ptr<juce::AudioProcessor> plain (createPluginFilter());
    asSynth (*plain).getPresetManager().setUserPresetFolder (folder);
    asSynth (*plain).getPresetManager().saveUserPreset ("Without a table");
    asSynth (*loader).getPresetManager().loadUserPreset ("Without a table");
    check (asSynth (*loader).getWavetableImporter().getTable() == loaded, "a preset without one leaves the loaded table alone");

    asSynth (*loader).getPresetManager().loadFactoryPreset (3);
    check (asSynth (*loader).getWavetableImporter().getTable() == loaded, "and so does a factory preset");

    const auto presetFile = folder.getChildFile ("With a table.xml");
    std::printf ("      a preset with a 2-frame table: %d bytes\n", (int) presetFile.getSize());
    folder.deleteRecursively();
}

void testLargestTableSaved()
{
    // The most a file can have: 256 frames, through a whole round trip.
    std::vector<std::vector<float>> frames;

    for (int i = 0; i < 256; ++i)
        frames.push_back (makeFrame ([i] (double phase) { return std::sin (phase) + 0.01 * i * std::sin (2.0 * phase); }));

    const WavFile file ({ joined (frames) });
    std::unique_ptr<juce::AudioProcessor> original (createPluginFilter());
    importAndWait (*original, file.file());

    juce::MemoryBlock state;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    auto restored = reopened (*original, &state);
    const auto table = asSynth (*restored).getWavetableImporter().getTable();
    std::printf ("      a project with 256 frames: %.1f MB, saved and reopened in %.0f ms\n", (double) state.getSize() / 1.0e6,
                 juce::Time::getMillisecondCounterHiRes() - start);
    check (table != nullptr && table->getNumFrames() == 256, "a project keeps a wavetable of 256 frames");
}

void testWindowShowsTheReopenedTable()
{
    // A project opened with the window open: the text by the Load wavetable button names its table.
    std::unique_ptr<juce::AudioProcessor> original (createPluginFilter()), other (createPluginFilter());
    const WavFile file ({ makeFrame (thirdHarmonicShape) });
    importAndWait (*original, file.file());
    juce::MemoryBlock state;
    original->getStateInformation (state);

    auto* editor = other->createEditorAndMakeActive();
    auto* status = editor != nullptr ? dynamic_cast<juce::Label*> (findDescendant (*editor, "wavetableStatus")) : nullptr;
    other->setStateInformation (state.getData(), (int) state.getSize());

    // The window hears about it as a message, which a host's message loop would deliver.
    asSynth (*other).getPresetManager().dispatchPendingMessages();

    check (status != nullptr && status->getText() == file.file().getFileName(), "an open window names the table a project brings");

    if (editor != nullptr)
    {
        other->editorBeingDeleted (editor);
        delete editor;
    }
}

//==============================================================================
void testLoadingFromTheWindow()
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    auto* editor = dynamic_cast<boringsynth::BoringSynthEditor*> (processor->createEditorAndMakeActive());

    if (editor == nullptr)
    {
        check (false, "the plug-in's window opens");
        return;
    }

    auto* button = dynamic_cast<juce::Button*> (findDescendant (*editor, "loadWavetable"));
    auto* status = dynamic_cast<juce::Label*> (findDescendant (*editor, "wavetableStatus"));
    auto* display = dynamic_cast<boringsynth::WavetableDisplay*> (findDescendant (*editor, "wavetableDisplay"));
    check (button != nullptr && status != nullptr && display != nullptr, "the window has a Load wavetable button, and text beside it");

    if (button != nullptr && status != nullptr && display != nullptr)
    {
        const auto waitForTheWindow = [&]
        {
            waitForImport (asSynth (*processor).getWavetableImporter());

            for (int waited = 0; waited < 3000 && status->getText().startsWith ("Loading"); waited += 10)
            {
                juce::Thread::sleep (10);
                juce::Timer::callPendingTimersSynchronously();
            }
        };

        // A file that won't load says why, and stays on the waveform it was on.
        const juce::TemporaryFile text (".wav");
        text.getFile().replaceWithText ("this isn't audio");
        editor->loadWavetable (text.getFile());
        waitForTheWindow();
        std::printf ("      after a bad file: '%s'\n", status->getText().toRawUTF8());
        check (status->getText().contains ("isn't an audio file") && getParameter (*processor, "waveform") == 0.0f,
               "a file that won't load says why, and leaves the waveform alone");

        // One that loads is named, and played.
        const WavFile file ({ makeFrame ([] (double phase) { return std::sin (3.0 * phase); }) });
        editor->loadWavetable (file.file());
        waitForTheWindow();
        check (status->getText() == file.file().getFileName(), "a file that loads is named");
        check (getParameter (*processor, "waveform") == (float) imported, "and the waveform switches to Imported");

        // And the picture draws it: a third harmonic, so low a quarter of the way across, where a sine,
        // which it would be without the table, is high.
        display->refresh();
        const auto image = display->createComponentSnapshot (display->getLocalBounds());
        int curveRow = 0;

        for (int y = 0; y < image.getHeight(); ++y)
            if (image.getPixelAt (image.getWidth() / 4, y).getBlue() > image.getPixelAt (image.getWidth() / 4, curveRow).getBlue())
                curveRow = y;

        std::printf ("      a third harmonic in the picture, a quarter of the way across: row %d of %d\n", curveRow, image.getHeight());
        check (display->getShownWaveform() == imported && curveRow > image.getHeight() / 2, "the picture shows the loaded table");

        // A window opened later says what's loaded.
        if (auto* open = processor->getActiveEditor())
        {
            processor->editorBeingDeleted (open);
            delete open;
        }

        editor = dynamic_cast<boringsynth::BoringSynthEditor*> (processor->createEditorAndMakeActive());
        auto* reopenedStatus = dynamic_cast<juce::Label*> (findDescendant (*editor, "wavetableStatus"));
        check (reopenedStatus != nullptr && reopenedStatus->getText() == file.file().getFileName(), "a window opened later names the file loaded");
    }

    if (auto* open = processor->getActiveEditor())
    {
        processor->editorBeingDeleted (open);
        delete open;
    }
}

} // namespace

void runImportTests()
{
    testReadingFrames();
    testChannelsAreMixed();
    testDcOffsetRemoved();
    testSingleCycle();
    testLoudnessLimits();
    testRefusedFiles();
    testFailedImportKeepsTheTable();
    testImportedPlays();
    testStartsAtZero();
    testNoteFadesIn();
    testReplacingATableCrossfades();
    testOldTablesKeptWhileInUse();
    testStolenNoteKeepsItsTable();
    testReplacingTablesWhilePlaying();
    testEveryCopyHasItsOwn();
    testSavedWithTheProject();
    testProjectWithoutATable();
    testDamagedSavedTable();
    testSavedInPresets();
    testLargestTableSaved();
    testWindowShowsTheReopenedTable();
    testLoadingFromTheWindow();
}

} // namespace testing
