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

float peakBetween (const std::vector<float>& samples, int start, int end)
{
    return peakOf (std::vector<float> (samples.begin() + start, samples.begin() + end));
}

// The largest change from one sample to the next. A sudden jump in level shows up here.
float largestStep (const std::vector<float>& samples)
{
    float largest = 0.0f;

    for (size_t i = 1; i < samples.size(); ++i)
        largest = std::max (largest, std::abs (samples[i] - samples[i - 1]));

    return largest;
}

// The times (in fractional samples) where the waveform crosses zero going upwards, at or after
// sample `from` and before sample `to`. One crossing per cycle of a sine.
std::vector<double> upwardCrossings (const std::vector<float>& samples, int from, int to)
{
    std::vector<double> times;

    for (auto i = std::max (from, 1); i < to; ++i)
    {
        const auto before = samples[(size_t) i - 1];
        const auto after = samples[(size_t) i];

        if (before < 0.0f && after >= 0.0f)
            times.push_back (i - 1 + before / (before - after));
    }

    return times;
}

// The frequency of a sine between two sample positions, from its zero crossings.
double frequencyBetween (const std::vector<float>& samples, int from, int to, double rate)
{
    const auto times = upwardCrossings (samples, from, to);

    if (times.size() < 2)
        return 0.0;

    return (double) (times.size() - 1) / (times.back() - times.front()) * rate;
}

// The length in samples of the first complete cycle that starts at or after `from`.
double firstPeriodAfter (const std::vector<float>& samples, int from)
{
    const auto times = upwardCrossings (samples, from, (int) samples.size());
    return times.size() < 2 ? 0.0 : times[1] - times[0];
}

int lastNonZeroIndex (const std::vector<float>& samples)
{
    for (auto i = (int) samples.size() - 1; i >= 0; --i)
        if (samples[(size_t) i] != 0.0f)
            return i;

    return -1;
}

juce::MidiMessage noteOn (int note, int velocity = 100)
{
    return juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity);
}

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

juce::MidiMessage controller (int number, int value, int channel = 1)
{
    return juce::MidiMessage::controllerEvent (channel, number, value);
}

constexpr int sustainPedal = 64;
constexpr int sostenutoPedal = 66;

// In these tests the key is released at sample 4800, so without a pedal the note ends by 5040,
// after its 5 ms (240 sample) fade.
constexpr int keyUp = 4800;
constexpr int endWithoutPedal = keyUp + 240;

void testSustainPedalHoldsNotes()
{
    // Pedal down before the note, key released at 4800, pedal released at 9600.
    const auto output = render (testSampleRate, 512, 14400, { { 0, controller (sustainPedal, 127) },
                                                              { 1, noteOn (69) },
                                                              { keyUp, noteOff (69) },
                                                              { 9600, controller (sustainPedal, 0) } });
    const auto end = lastNonZeroIndex (output);
    std::printf ("      sustained note ends at sample %d (pedal released at 9600)\n", end);
    check (peakBetween (output, endWithoutPedal, 9600) > 0.09f, "the sustain pedal holds a note after its key is released");
    check (end > 9600 && end <= 9600 + 240, "releasing the pedal ends the note with the usual fade");
}

void testSustainPedalPressedDuringNote()
{
    const auto output = render (testSampleRate, 512, 14400, { { 0, noteOn (69) },
                                                              { 100, controller (sustainPedal, 127) },
                                                              { keyUp, noteOff (69) },
                                                              { 9600, controller (sustainPedal, 0) } });
    check (peakBetween (output, endWithoutPedal, 9600) > 0.09f, "pressing the pedal while a key is held sustains that note");
}

void testSustainPedalAfterRelease()
{
    // A pedal pressed after the key is released shouldn't bring the note back or hold its fade.
    const auto output = render (testSampleRate, 512, 9600, { { 0, noteOn (69) },
                                                             { keyUp, noteOff (69) },
                                                             { 4900, controller (sustainPedal, 127) } });
    check (lastNonZeroIndex (output) <= endWithoutPedal, "pressing the pedal after a key is released doesn't hold the note");
}

void testSustainPedalThreshold()
{
    // MIDI treats pedal values of 64 and up as down, and 63 and below as up.
    const auto output = render (testSampleRate, 512, 9600, { { 0, controller (sustainPedal, 63) },
                                                             { 1, noteOn (69) },
                                                             { keyUp, noteOff (69) } });
    check (lastNonZeroIndex (output) <= endWithoutPedal, "a pedal value of 63 counts as up");
}

void testSustainPedalIsPerChannel()
{
    const auto output = render (testSampleRate, 512, 9600, { { 0, controller (sustainPedal, 127, 2) },
                                                             { 1, noteOn (69) },
                                                             { keyUp, noteOff (69) } });
    check (lastNonZeroIndex (output) <= endWithoutPedal, "a sustain pedal on another MIDI channel doesn't hold the note");
}

void testSostenutoPedalIgnored()
{
    const auto output = render (testSampleRate, 512, 9600, { { 0, noteOn (69) },
                                                             { 1, controller (sostenutoPedal, 127) },
                                                             { keyUp, noteOff (69) } });
    check (lastNonZeroIndex (output) <= endWithoutPedal, "the sostenuto pedal is ignored");
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
                                             { 2000, juce::MidiMessage::pitchWheel (1, 12000) },
                                             { 2003, juce::MidiMessage::pitchWheel (1, 3000) },
                                             { 2500, controller (sustainPedal, 127) },
                                             { 3000, noteOff (60) },
                                             { 4000, juce::MidiMessage::pitchWheel (1, 8192) },
                                             { 5000, noteOff (64) },
                                             { 5001, noteOff (67) },
                                             { 6000, controller (sustainPedal, 0) } };

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

// Renders a held note, with the volume jumping from 0 dB to silence at the start of a block,
// the way a host delivers automation.
constexpr int volumeChangeSample = 47 * 512;

std::vector<float> renderVolumeDrop()
{
    return render (testSampleRate, 512, 48000, { { 0, noteOn (69) } }, true, {},
                   [] (juce::AudioProcessor& p, int blockStart)
                   {
                       if (blockStart == volumeChangeSample)
                           setParameter (p, "volume", -60.0f);
                   });
}

void testVolumeChangeIsSmooth()
{
    // The sine itself changes by up to a certain amount per sample. A smoothed volume change
    // barely adds to that; an instant one jumps by up to the note's whole level (0.1).
    const auto steady = largestStep (render (testSampleRate, 512, 48000, { { 0, noteOn (69) } }));
    const auto dropped = largestStep (renderVolumeDrop());
    std::printf ("      largest sample-to-sample step: %.5f steady, %.5f with the volume drop\n", steady, dropped);
    check (dropped <= steady * 1.05f, "a sudden volume change doesn't jump the output");
}

void testVolumeGlideLength()
{
    // 20 ms at 48 kHz is 960 samples: the level should reach silence by then, not before.
    const auto output = renderVolumeDrop();
    const auto glide = lastNonZeroIndex (output) - volumeChangeSample;
    std::printf ("      volume glides to silence in %d samples (%.2f ms)\n", glide, glide / testSampleRate * 1000.0);
    check (glide > 900 && glide <= 960, "a volume change glides over ~20 ms");
}

void testSavedVolumeDoesNotFadeIn()
{
    // A volume set before playback starts (like one loaded from a project) should apply from the
    // first sample. 200 samples is under a fifth of the glide, but more than one cycle of the note.
    const auto output = render (testSampleRate, 512, 48000, { { 0, noteOn (69) } }, true,
                                [] (juce::AudioProcessor& p) { setParameter (p, "volume", -12.0f); });
    const auto startPeak = peakBetween (output, 0, 200);
    const auto expected = 0.1f * juce::Decibels::decibelsToGain (-12.0f);
    std::printf ("      first 200 samples peak at %.4f (expected %.4f)\n", startPeak, expected);
    check (std::abs (startPeak - expected) < 0.0005f, "a volume set before playback doesn't fade in");
}

// The peak level of one note at the given velocity and velocity sensitivity (percent).
float notePeak (int velocity, float sensitivity)
{
    return peakOf (render (testSampleRate, 512, 9600, { { 0, noteOn (69, velocity) } }, true,
                           [sensitivity] (juce::AudioProcessor& p) { setParameter (p, "velocitySensitivity", sensitivity); }));
}

void testVelocityIgnoredByDefault()
{
    const auto soft = peakOf (render (testSampleRate, 512, 9600, { { 0, noteOn (69, 1) } }));
    const auto hard = peakOf (render (testSampleRate, 512, 9600, { { 0, noteOn (69, 127) } }));
    std::printf ("      default sensitivity: velocity 1 peaks at %.4f, velocity 127 at %.4f\n", soft, hard);
    check (std::abs (soft - 0.1f) < 0.0005f && std::abs (hard - 0.1f) < 0.0005f,
           "velocity is ignored at the default sensitivity (0 %)");
}

void testFullVelocitySensitivity()
{
    // At 100 %, level follows (velocity / 127) squared, so velocity 64 is about -12 dB.
    const auto hard = notePeak (127, 100.0f);
    const auto medium = notePeak (64, 100.0f);
    const auto expectedMedium = 0.1f * std::pow (64.0f / 127.0f, 2.0f);
    std::printf ("      100 %%: velocity 127 peaks at %.4f, velocity 64 at %.4f (expected %.4f)\n",
                 hard, medium, expectedMedium);
    check (std::abs (hard - 0.1f) < 0.0005f, "the hardest hit stays at full level at 100 % sensitivity");
    check (std::abs (medium - expectedMedium) < 0.0005f, "velocity 64 is about -12 dB at 100 % sensitivity");
}

void testHalfVelocitySensitivity()
{
    const auto medium = notePeak (64, 50.0f);
    const auto expected = 0.1f * (0.5f + 0.5f * std::pow (64.0f / 127.0f, 2.0f));
    std::printf ("      50 %%: velocity 64 peaks at %.4f (expected %.4f)\n", medium, expected);
    check (std::abs (medium - expected) < 0.0005f, "50 % sensitivity is halfway between ignoring velocity and full");
}

void testSensitivityChangeKeepsHeldNotes()
{
    // A note's level is set when it starts. Turning sensitivity up mid-note shouldn't change it.
    const auto output = render (testSampleRate, 512, 48000, { { 0, noteOn (69, 32) } }, true, {},
                                [] (juce::AudioProcessor& p, int blockStart)
                                {
                                    if (blockStart == volumeChangeSample)
                                        setParameter (p, "velocitySensitivity", 100.0f);
                                });
    const auto afterChange = peakBetween (output, volumeChangeSample, (int) output.size());
    check (std::abs (afterChange - 0.1f) < 0.0005f, "changing sensitivity doesn't affect a note already playing");
}

juce::MidiMessage pitchWheel (int value) { return juce::MidiMessage::pitchWheel (1, value); }

// A4 (440 Hz) played with the wheel already at `wheelValue`; returns its frequency.
double bentFrequency (int wheelValue, int bendRange = 2)
{
    const auto output = render (testSampleRate, 512, 48000, { { 0, pitchWheel (wheelValue) }, { 0, noteOn (69) } }, true,
                                [bendRange] (juce::AudioProcessor& p)
                                {
                                    setParameter (p, "pitchBendRange", (float) bendRange);
                                });
    return frequencyBetween (output, 0, 48000, testSampleRate);
}

void testPitchBend()
{
    const auto centre = bentFrequency (8192);
    const auto fullUp = bentFrequency (16383);
    const auto fullDown = bentFrequency (0);
    std::printf ("      wheel centre %.2f Hz, full up %.2f Hz (expected 493.88), full down %.2f Hz (expected 392.00)\n",
                 centre, fullUp, fullDown);
    check (std::abs (centre - 440.0) < 0.05, "pitch wheel at centre leaves the pitch alone");
    check (std::abs (fullUp - 493.88) < 0.05, "pitch wheel fully up bends up 2 semitones by default");
    check (std::abs (fullDown - 392.00) < 0.05, "pitch wheel fully down bends down 2 semitones by default");
}

void testPitchBendRange()
{
    const auto octaveUp = bentFrequency (16383, 12);
    const auto rangeZero = bentFrequency (16383, 0);
    std::printf ("      range 12, full up: %.2f Hz (expected 880.00); range 0, full up: %.2f Hz (expected 440.00)\n",
                 octaveUp, rangeZero);
    check (std::abs (octaveUp - 880.0) < 0.05, "a bend range of 12 bends a full octave");
    check (std::abs (rangeZero - 440.0) < 0.05, "a bend range of 0 ignores the pitch wheel");
}

void testBendBeforeNoteStartsBent()
{
    // The wheel is already up when the note starts, so its first cycles should already be bent,
    // not gliding up from the unbent pitch.
    const auto output = render (testSampleRate, 512, 4800, { { 0, pitchWheel (16383) }, { 0, noteOn (69) } });
    const auto firstPeriod = firstPeriodAfter (output, 0);
    std::printf ("      first cycle lasts %.2f samples (bent: 97.19, unbent: 109.09)\n", firstPeriod);
    check (std::abs (firstPeriod - 97.19) < 0.1, "a note played with the wheel already up starts bent");
}

void testBendGlides()
{
    // The wheel jumps fully up mid-note. The pitch should glide there over ~10 ms rather than jump.
    const auto output = render (testSampleRate, 512, 48000, { { 0, noteOn (69) }, { volumeChangeSample, pitchWheel (16383) } });
    const auto firstPeriod = firstPeriodAfter (output, volumeChangeSample);
    const auto settled = frequencyBetween (output, volumeChangeSample + 960, volumeChangeSample + 960 + 9600, testSampleRate);
    std::printf ("      first cycle after the jump lasts %.2f samples (unbent 109.09, bent 97.19); 20 ms later %.2f Hz\n",
                 firstPeriod, settled);
    check (firstPeriod > 97.19 * 1.02, "a sudden wheel move glides rather than jumping");
    check (std::abs (settled - 493.88) < 0.05, "the glide reaches the bent pitch within 20 ms");
}

void testBendAboveNyquist()
{
    // At 16 kHz, note 100 (2637 Hz) bent up 24 semitones would be 10548 Hz, above Nyquist (8 kHz).
    // It should be silent while bent that far, then play again once the wheel comes back.
    const auto output = render (16000.0, 512, 32000, { { 0, pitchWheel (16383) }, { 0, noteOn (100) }, { 16000, pitchWheel (8192) } }, true,
                                [] (juce::AudioProcessor& p) { setParameter (p, "pitchBendRange", 24.0f); });
    bool allFinite = true;

    for (auto s : output)
        allFinite = allFinite && std::isfinite (s);

    check (allFinite && peakBetween (output, 0, 16000) == 0.0f, "a note bent above Nyquist is silent");
    check (peakBetween (output, 17000, 32000) > 0.09f, "it plays again when bent back below Nyquist");
}

} // namespace

void runRenderTests()
{
    testChordLevels();
    testNyquist();
    testNoSampleRate();
    testReleaseFade();
    testSustainPedalHoldsNotes();
    testSustainPedalPressedDuringNote();
    testSustainPedalAfterRelease();
    testSustainPedalThreshold();
    testSustainPedalIsPerChannel();
    testSostenutoPedalIgnored();
    testNoteTiming();
    testBlockSizeIndependence();
    testDefaultVolumeKeepsLevel();
    testVolumeChangesLevel();
    testMinimumVolumeIsSilent();
    testVolumeChangeIsSmooth();
    testVolumeGlideLength();
    testSavedVolumeDoesNotFadeIn();
    testVelocityIgnoredByDefault();
    testFullVelocitySensitivity();
    testHalfVelocitySensitivity();
    testSensitivityChangeKeepsHeldNotes();
    testPitchBend();
    testPitchBendRange();
    testBendBeforeNoteStartsBent();
    testBendGlides();
    testBendAboveNyquist();
}

} // namespace testing
