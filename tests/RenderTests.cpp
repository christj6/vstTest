#include "TestHelpers.h"

// Render tests: each feeds the processor MIDI, renders audio, and checks the output.

namespace testing
{
namespace
{
constexpr double testSampleRate = 48000.0;

float peakOf (const std::vector<float>& samples)
{
    float peak = 0.0f;

    for (auto s : samples)
        peak = std::max (peak, std::abs (s));

    return peak;
}

int lastNonZeroIndex (const std::vector<float>& samples)
{
    for (auto i = (int) samples.size() - 1; i >= 0; --i)
        if (samples[(size_t) i] != 0.0f)
            return i;

    return -1;
}

juce::MidiMessage noteOn (int note)  { return juce::MidiMessage::noteOn (1, note, (juce::uint8) 100); }
juce::MidiMessage noteOff (int note) { return juce::MidiMessage::noteOff (1, note); }

//==============================================================================
void testChordLevels()
{
    std::vector<TimedMessage> cluster;

    for (int i = 0; i < 8; ++i)
        cluster.push_back ({ 0, noteOn (60 + i) });

    const auto clusterPeak = peakOf (render (testSampleRate, 512, 48000, cluster));
    std::printf ("      8-note cluster peak = %.3f\n", clusterPeak);
    check (clusterPeak < 1.0f, "8-note chord stays below full scale");

    std::vector<TimedMessage> chord;

    for (int i = 0; i < 16; ++i)
        chord.push_back ({ 0, noteOn (48 + i * 3) });

    const auto chordPeak = peakOf (render (testSampleRate, 512, 48000, chord));
    std::printf ("      16-note chord peak = %.3f\n", chordPeak);
    check (chordPeak <= 1.0f, "16-note chord never exceeds full scale");
}

void testNyquist()
{
    // At 16 kHz, Nyquist is 8 kHz. Note 127 is about 12.5 kHz, note 100 about 2.6 kHz.
    check (peakOf (render (16000.0, 512, 16000, { { 0, noteOn (127) } })) == 0.0f,
           "note 127 at 16 kHz (above Nyquist) is silent");
    check (peakOf (render (16000.0, 512, 16000, { { 0, noteOn (100) } })) > 0.0f,
           "note 100 at 16 kHz (below Nyquist) plays");
}

void testNoSampleRate()
{
    const auto output = render (testSampleRate, 512, 4800, { { 0, noteOn (60) } }, false);
    bool allFinite = true;

    for (auto s : output)
        allFinite = allFinite && std::isfinite (s);

    check (allFinite && peakOf (output) == 0.0f,
           "note before prepareToPlay (sample rate 0) gives silence, no NaN");
}

void testReleaseFade()
{
    const auto output = render (testSampleRate, 512, 9600, { { 0, noteOn (69) }, { 4800, noteOff (69) } });
    const auto tail = lastNonZeroIndex (output) - 4800;
    std::printf ("      release tail = %d samples (%.2f ms)\n", tail, tail / testSampleRate * 1000.0);

    // 5 ms at 48 kHz is 240 samples.
    check (tail > 200 && tail <= 240, "note off fades out over ~5 ms");
}

void testSustainPedalIgnored()
{
    const auto output = render (testSampleRate, 512, 9600, { { 0, juce::MidiMessage::controllerEvent (1, 64, 127) },
                                                         { 1, noteOn (69) },
                                                         { 4800, noteOff (69) } });
    check (lastNonZeroIndex (output) <= 4800 + 240, "sustain pedal is ignored");
}

void testNoteTiming()
{
    // JUCE's Synthesiser, by default, handles an event less than 32 samples after the previous
    // split early. The second note here is 10 samples after the first, so it would start early.
    const auto oneNote = render (testSampleRate, 512, 2048, { { 100, noteOn (60) } });
    const auto twoNotes = render (testSampleRate, 512, 2048, { { 100, noteOn (60) }, { 110, noteOn (72) } });

    int firstDifference = -1;

    for (int i = 0; i < 2048; ++i)
    {
        if (twoNotes[(size_t) i] != oneNote[(size_t) i])
        {
            firstDifference = i;
            break;
        }
    }

    // The sine starts at phase 0, so a note starting at sample 110 first changes sample 111.
    std::printf ("      second note first changes output at sample %d (expected 111)\n", firstDifference);
    check (firstDifference == 111, "a note 10 samples after another starts on its exact sample");
}

void testBlockSizeIndependence()
{
    const std::vector<TimedMessage> events { { 3, noteOn (60) },
                                             { 100, noteOn (64) },
                                             { 101, noteOn (67) },
                                             { 3000, noteOff (60) },
                                             { 5000, noteOff (64) },
                                             { 5001, noteOff (67) } };

    const auto blocksOf1 = render (testSampleRate, 1, 8000, events);
    const auto blocksOf7 = render (testSampleRate, 7, 8000, events);
    const auto blocksOf512 = render (testSampleRate, 512, 8000, events);

    check (blocksOf1 == blocksOf7 && blocksOf7 == blocksOf512,
           "output identical with block sizes 1, 7 and 512");
}

void testDefaultVolumeKeepsLevel()
{
    // Each note is 0.1 at full scale. At the default volume of 0 dB, one note should still be.
    const auto notePeak = peakOf (render (testSampleRate, 512, 48000, { { 0, noteOn (69) } }));
    std::printf ("      one note at default volume peaks at %.4f\n", notePeak);
    check (std::abs (notePeak - 0.1f) < 0.001f, "default volume (0 dB) leaves the level unchanged");
}

void testVolumeChangesLevel()
{
    const auto atDefault = peakOf (render (testSampleRate, 512, 48000, { { 0, noteOn (69) } }));
    const auto atMinus6 = peakOf (render (testSampleRate, 512, 48000, { { 0, noteOn (69) } }, true,
                                          [] (juce::AudioProcessor& p) { setParameter (p, "volume", -6.0f); }));
    const auto ratio = atMinus6 / atDefault;
    std::printf ("      -6 dB gives %.4f of the default level (expected 0.5012)\n", ratio);
    check (std::abs (ratio - juce::Decibels::decibelsToGain (-6.0f)) < 0.001f, "volume -6 dB halves the level");
}

void testMinimumVolumeIsSilent()
{
    const auto output = render (testSampleRate, 512, 48000, { { 0, noteOn (69) } }, true,
                                [] (juce::AudioProcessor& p) { setParameter (p, "volume", -60.0f); });
    check (peakOf (output) == 0.0f, "minimum volume is silent");
}
} // namespace

void runRenderTests()
{
    testChordLevels();
    testNyquist();
    testNoSampleRate();
    testReleaseFade();
    testSustainPedalIgnored();
    testNoteTiming();
    testBlockSizeIndependence();
    testDefaultVolumeKeepsLevel();
    testVolumeChangesLevel();
    testMinimumVolumeIsSilent();
}
} // namespace testing
