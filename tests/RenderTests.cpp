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

    const auto clusterPeak = peakOf (render ({ .numSamples = 48000, .events = cluster }));
    std::printf ("      8-note cluster peak = %.3f\n", clusterPeak);
    check (clusterPeak < 1.0f, "8-note chord stays below full scale");

    std::vector<TimedMessage> chord;

    for (int i = 0; i < 16; ++i)
        chord.push_back ({ 0, noteOn (48 + i * 3) });

    const auto chordPeak = peakOf (render ({ .numSamples = 48000, .events = chord }));
    std::printf ("      16-note chord peak = %.3f\n", chordPeak);
    check (chordPeak <= 1.0f, "16-note chord never exceeds full scale");
}

void testNyquist()
{
    // At 16 kHz, Nyquist is 8 kHz. Note 127 is about 12.5 kHz, note 100 about 2.6 kHz.
    check (peakOf (render ({ .sampleRate = 16000.0, .numSamples = 16000, .events = { { 0, noteOn (127) } } })) == 0.0f,
           "note 127 at 16 kHz (above Nyquist) is silent");
    check (peakOf (render ({ .sampleRate = 16000.0, .numSamples = 16000, .events = { { 0, noteOn (100) } } })) > 0.0f,
           "note 100 at 16 kHz (below Nyquist) plays");
}

void testNoSampleRate()
{
    const auto output = render ({ .numSamples = 4800, .events = { { 0, noteOn (60) } }, .prepare = false });
    bool allFinite = true;

    for (auto s : output)
        allFinite = allFinite && std::isfinite (s);

    check (allFinite && peakOf (output) == 0.0f,
           "note before prepareToPlay (sample rate 0) gives silence, no NaN");
}

void testReleaseFade()
{
    const auto output = render ({ .numSamples = 9600, .events = { { 0, noteOn (69) }, { 4800, noteOff (69) } } });
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
    const auto output = render ({ .numSamples = 14400,
                                  .events = { { 0, controller (sustainPedal, 127) },
                                              { 1, noteOn (69) },
                                              { keyUp, noteOff (69) },
                                              { 9600, controller (sustainPedal, 0) } } });
    const auto end = lastNonZeroIndex (output);
    std::printf ("      sustained note ends at sample %d (pedal released at 9600)\n", end);
    check (peakBetween (output, endWithoutPedal, 9600) > 0.09f, "the sustain pedal holds a note after its key is released");
    check (end > 9600 && end <= 9600 + 240, "releasing the pedal ends the note with the usual fade");
}

void testSustainPedalPressedDuringNote()
{
    const auto output = render ({ .numSamples = 14400,
                                  .events = { { 0, noteOn (69) },
                                              { 100, controller (sustainPedal, 127) },
                                              { keyUp, noteOff (69) },
                                              { 9600, controller (sustainPedal, 0) } } });
    check (peakBetween (output, endWithoutPedal, 9600) > 0.09f, "pressing the pedal while a key is held sustains that note");
}

void testSustainPedalAfterRelease()
{
    // A pedal pressed after the key is released shouldn't bring the note back or hold its fade.
    const auto output = render ({ .numSamples = 9600,
                                  .events = { { 0, noteOn (69) },
                                              { keyUp, noteOff (69) },
                                              { 4900, controller (sustainPedal, 127) } } });
    check (lastNonZeroIndex (output) <= endWithoutPedal, "pressing the pedal after a key is released doesn't hold the note");
}

void testSustainPedalThreshold()
{
    // MIDI treats pedal values of 64 and up as down, and 63 and below as up.
    const auto output = render ({ .numSamples = 9600,
                                  .events = { { 0, controller (sustainPedal, 63) },
                                              { 1, noteOn (69) },
                                              { keyUp, noteOff (69) } } });
    check (lastNonZeroIndex (output) <= endWithoutPedal, "a pedal value of 63 counts as up");
}

void testSustainPedalIsPerChannel()
{
    const auto output = render ({ .numSamples = 9600,
                                  .events = { { 0, controller (sustainPedal, 127, 2) },
                                              { 1, noteOn (69) },
                                              { keyUp, noteOff (69) } } });
    check (lastNonZeroIndex (output) <= endWithoutPedal, "a sustain pedal on another MIDI channel doesn't hold the note");
}

void testSostenutoPedalIgnored()
{
    const auto output = render ({ .numSamples = 9600,
                                  .events = { { 0, noteOn (69) },
                                              { 1, controller (sostenutoPedal, 127) },
                                              { keyUp, noteOff (69) } } });
    check (lastNonZeroIndex (output) <= endWithoutPedal, "the sostenuto pedal is ignored");
}

void testNoteTiming()
{
    // JUCE's Synthesiser, by default, handles an event less than 32 samples after the previous
    // split early. The second note here is 10 samples after the first, so it would start early.
    const auto oneNote = render ({ .numSamples = 2048, .events = { { 100, noteOn (60) } } });
    const auto twoNotes = render ({ .numSamples = 2048, .events = { { 100, noteOn (60) }, { 110, noteOn (72) } } });

    // A sine starts at zero, so a note starting at sample 110 is first heard at sample 111. Its
    // first sample there is about 0.007; the sine tables are only zero at the start to within
    // rounding (around 1e-8), so "heard" means a difference above 1e-6.
    int firstAudible = -1;

    for (int i = 0; i < 2048; ++i)
    {
        if (std::abs (twoNotes[(size_t) i] - oneNote[(size_t) i]) > 1.0e-6f)
        {
            firstAudible = i;
            break;
        }
    }

    std::printf ("      second note is first heard at sample %d (expected 111)\n", firstAudible);
    check (firstAudible == 111, "a note 10 samples after another starts on its exact sample");
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

    const auto blocksOf1 = render ({ .blockSize = 1, .numSamples = 8000, .events = events });
    const auto blocksOf7 = render ({ .blockSize = 7, .numSamples = 8000, .events = events });
    const auto blocksOf512 = render ({ .blockSize = 512, .numSamples = 8000, .events = events });

    check (blocksOf1 == blocksOf7 && blocksOf7 == blocksOf512,
           "output identical with block sizes 1, 7 and 512");
}

// One A4 note held for a second, the starting point for most of the level tests.
const std::vector<TimedMessage> heldNote { { 0, noteOn (69) } };

void testDefaultVolumeKeepsLevel()
{
    // Each note is 0.1 at full scale. At the default volume of 0 dB, one note should still be.
    const auto notePeak = peakOf (render ({ .numSamples = 48000, .events = heldNote }));
    std::printf ("      one note at default volume peaks at %.4f\n", notePeak);
    check (std::abs (notePeak - 0.1f) < 0.001f, "default volume (0 dB) leaves the level unchanged");
}

void testVolumeChangesLevel()
{
    const auto atDefault = peakOf (render ({ .numSamples = 48000, .events = heldNote }));
    const auto atMinus6 = peakOf (render ({ .numSamples = 48000,
                                            .events = heldNote,
                                            .setUp = [] (juce::AudioProcessor& p) { setParameter (p, "volume", -6.0f); } }));
    const auto ratio = atMinus6 / atDefault;
    std::printf ("      -6 dB gives %.4f of the default level (expected 0.5012)\n", ratio);
    check (std::abs (ratio - juce::Decibels::decibelsToGain (-6.0f)) < 0.001f, "volume -6 dB halves the level");
}

void testMinimumVolumeIsSilent()
{
    const auto output = render ({ .numSamples = 48000,
                                  .events = heldNote,
                                  .setUp = [] (juce::AudioProcessor& p) { setParameter (p, "volume", -60.0f); } });
    check (peakOf (output) == 0.0f, "minimum volume is silent");
}

// Renders a held note, with the volume jumping from 0 dB to silence at the start of a block,
// the way a host delivers automation.
constexpr int volumeChangeSample = 47 * 512;

std::vector<float> renderVolumeDrop()
{
    return render ({ .numSamples = 48000,
                     .events = heldNote,
                     .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                     {
                         if (blockStart == volumeChangeSample)
                             setParameter (p, "volume", -60.0f);
                     } });
}

void testVolumeChangeIsSmooth()
{
    // The sine itself changes by up to a certain amount per sample. A smoothed volume change
    // barely adds to that; an instant one jumps by up to the note's whole level (0.1).
    const auto steady = largestStep (render ({ .numSamples = 48000, .events = heldNote }));
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
    const auto output = render ({ .numSamples = 48000,
                                  .events = heldNote,
                                  .setUp = [] (juce::AudioProcessor& p) { setParameter (p, "volume", -12.0f); } });
    const auto startPeak = peakBetween (output, 0, 200);
    const auto expected = 0.1f * juce::Decibels::decibelsToGain (-12.0f);
    std::printf ("      first 200 samples peak at %.4f (expected %.4f)\n", startPeak, expected);
    check (std::abs (startPeak - expected) < 0.0005f, "a volume set before playback doesn't fade in");
}

// The peak level of one note at the given velocity and velocity sensitivity (percent).
float notePeak (int velocity, float sensitivity)
{
    return peakOf (render ({ .numSamples = 9600,
                             .events = { { 0, noteOn (69, velocity) } },
                             .setUp = [sensitivity] (juce::AudioProcessor& p)
                             {
                                 setParameter (p, "velocitySensitivity", sensitivity);
                             } }));
}

void testVelocityIgnoredByDefault()
{
    const auto soft = peakOf (render ({ .numSamples = 9600, .events = { { 0, noteOn (69, 1) } } }));
    const auto hard = peakOf (render ({ .numSamples = 9600, .events = { { 0, noteOn (69, 127) } } }));
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
    const auto output = render ({ .numSamples = 48000,
                                  .events = { { 0, noteOn (69, 32) } },
                                  .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                  {
                                      if (blockStart == volumeChangeSample)
                                          setParameter (p, "velocitySensitivity", 100.0f);
                                  } });
    const auto afterChange = peakBetween (output, volumeChangeSample, (int) output.size());
    check (std::abs (afterChange - 0.1f) < 0.0005f, "changing sensitivity doesn't affect a note already playing");
}

juce::MidiMessage pitchWheel (int value) { return juce::MidiMessage::pitchWheel (1, value); }

// A4 (440 Hz) played with the wheel already at `wheelValue`; returns its frequency.
double bentFrequency (int wheelValue, int bendRange = 2)
{
    const auto output = render ({ .numSamples = 48000,
                                  .events = { { 0, pitchWheel (wheelValue) }, { 0, noteOn (69) } },
                                  .setUp = [bendRange] (juce::AudioProcessor& p)
                                  {
                                      setParameter (p, "pitchBendRange", (float) bendRange);
                                  } });
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
    const auto output = render ({ .numSamples = 4800, .events = { { 0, pitchWheel (16383) }, { 0, noteOn (69) } } });
    const auto firstPeriod = firstPeriodAfter (output, 0);
    std::printf ("      first cycle lasts %.2f samples (bent: 97.19, unbent: 109.09)\n", firstPeriod);
    check (std::abs (firstPeriod - 97.19) < 0.1, "a note played with the wheel already up starts bent");
}

void testBendGlides()
{
    // The wheel jumps fully up mid-note. The pitch should glide there over ~10 ms rather than jump.
    const auto output = render ({ .numSamples = 48000,
                                  .events = { { 0, noteOn (69) }, { volumeChangeSample, pitchWheel (16383) } } });
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
    const auto output = render ({ .sampleRate = 16000.0,
                                  .numSamples = 32000,
                                  .events = { { 0, pitchWheel (16383) }, { 0, noteOn (100) }, { 16000, pitchWheel (8192) } },
                                  .setUp = [] (juce::AudioProcessor& p) { setParameter (p, "pitchBendRange", 24.0f); } });
    bool allFinite = true;

    for (auto s : output)
        allFinite = allFinite && std::isfinite (s);

    check (allFinite && peakBetween (output, 0, 16000) == 0.0f, "a note bent above Nyquist is silent");
    check (peakBetween (output, 17000, 32000) > 0.09f, "it plays again when bent back below Nyquist");
}

// The Waveform parameter's choices, by position: Sine, Triangle, Saw, Square.
constexpr int sine = 0, saw = 2, square = 3;

ProcessorSetUp withWaveform (int waveform)
{
    return [waveform] (juce::AudioProcessor& p) { setParameter (p, "waveform", (float) waveform); };
}

double rmsBetween (const std::vector<float>& samples, int start, int end)
{
    double sumOfSquares = 0.0;

    for (auto i = start; i < end; ++i)
        sumOfSquares += (double) samples[(size_t) i] * samples[(size_t) i];

    return std::sqrt (sumOfSquares / (end - start));
}

void testDefaultWaveformIsSine()
{
    const auto byDefault = render ({ .events = heldNote });
    const auto chosenSine = render ({ .events = heldNote, .setUp = withWaveform (sine) });
    check (byDefault == chosenSine, "the default waveform is the sine");
}

void testWaveformsAreEquallyLoud()
{
    const auto sineRms = rmsBetween (render ({ .events = heldNote }), 4800, 48000);
    double worst = 0.0;

    const char* names[] = { "Sine", "Triangle", "Saw", "Square" };

    for (int waveform = 1; waveform < 4; ++waveform)
    {
        const auto rms = rmsBetween (render ({ .events = heldNote, .setUp = withWaveform (waveform) }), 4800, 48000);
        const auto difference = 20.0 * std::log10 (rms / sineRms);
        std::printf ("      %-8s at 440 Hz: %+.2f dB from the sine\n", names[waveform], difference);
        worst = std::max (worst, std::abs (difference));
    }

    check (worst < 0.2, "every waveform plays within 0.2 dB of the sine's loudness");
}

void testWaveformChangeCrossfades()
{
    // Switch a held saw note to square at the start of a block. Over the next 480 samples (10 ms)
    // it should blend from one to the other, then be exactly what a square note would have been.
    const auto sawOnly = render ({ .events = heldNote, .setUp = withWaveform (saw) });
    const auto squareOnly = render ({ .events = heldNote, .setUp = withWaveform (square) });
    const auto switched = render ({ .events = heldNote,
                                    .setUp = withWaveform (saw),
                                    .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                    {
                                        if (blockStart == volumeChangeSample)
                                            setParameter (p, "waveform", (float) square);
                                    } });

    constexpr int fadeLength = 480;
    const auto halfway = volumeChangeSample + fadeLength / 2 - 1;
    const auto blendAtHalfway = 0.5f * (sawOnly[(size_t) halfway] + squareOnly[(size_t) halfway]);

    check (std::equal (switched.begin(), switched.begin() + volumeChangeSample, sawOnly.begin()),
           "before a waveform change, the note is unchanged");
    check (std::abs (switched[(size_t) halfway] - blendAtHalfway) < 1.0e-6f,
           "halfway through the 10 ms crossfade, the note is an even blend of both waveforms");
    check (std::equal (switched.begin() + volumeChangeSample + fadeLength - 1, switched.end(),
                       squareOnly.begin() + volumeChangeSample + fadeLength - 1),
           "after the crossfade, the note is exactly the new waveform");
}

void testNoteAfterWaveformChangeStartsDirectly()
{
    // A saw note plays and ends, then the waveform changes to square, then a second note starts.
    // The second note should be square from its first sample, with no crossfade from the saw the
    // voice played last. Compare it with the same two notes played as squares all along.
    constexpr int secondNote = 20 * 512;
    const std::vector<TimedMessage> twoNotes { { 0, noteOn (69) }, { 4800, noteOff (69) }, { secondNote, noteOn (69) } };
    const auto changedBetween = render ({ .events = twoNotes,
                                          .setUp = withWaveform (saw),
                                          .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                          {
                                              if (blockStart == 10 * 512)
                                                  setParameter (p, "waveform", (float) square);
                                          } });
    const auto squareAllAlong = render ({ .events = twoNotes, .setUp = withWaveform (square) });
    check (std::equal (changedBetween.begin() + secondNote, changedBetween.end(), squareAllAlong.begin() + secondNote),
           "a note started after a waveform change plays the new waveform at once");
}

ProcessorSetUp withEnvelope (float attack, float decay, float sustainPercent, float release)
{
    return [=] (juce::AudioProcessor& p)
    {
        setParameter (p, "ampAttack", attack);
        setParameter (p, "ampDecay", decay);
        setParameter (p, "ampSustain", sustainPercent);
        setParameter (p, "ampRelease", release);
    };
}

void testAmpAttack()
{
    // A 100 ms attack: quiet for the first 10 ms, at full level (0.1) once it's over.
    const auto output = render ({ .events = heldNote, .setUp = withEnvelope (0.1f, 0.2f, 100.0f, 0.005f) });
    const auto first10ms = peakBetween (output, 0, 480);
    const auto afterAttack = peakBetween (output, 4800, 9600);
    std::printf ("      100 ms attack: peak %.4f in the first 10 ms, %.4f after the attack\n", first10ms, afterAttack);
    check (first10ms < 0.011f && std::abs (afterAttack - 0.1f) < 0.001f, "the amp attack fades a note in over its attack time");
}

void testAmpDecayAndSustain()
{
    // Decay over 100 ms to a sustain of 50 %: the note settles at half its full level.
    const auto output = render ({ .events = heldNote, .setUp = withEnvelope (0.0f, 0.1f, 50.0f, 0.005f) });
    const auto settled = peakBetween (output, 9600, 48000);
    std::printf ("      sustain 50 %%: settles at a peak of %.4f (expected 0.0500)\n", settled);
    check (std::abs (settled - 0.05f) < 0.0005f, "after the decay, a held note plays at the sustain level");
}

void testAmpRelease()
{
    // A 500 ms release: the note keeps sounding for 24000 samples after its key is released.
    const auto output = render ({ .events = { { 0, noteOn (69) }, { 4800, noteOff (69) } },
                                  .setUp = withEnvelope (0.0f, 0.2f, 100.0f, 0.5f) });
    const auto tail = lastNonZeroIndex (output) - 4800;
    std::printf ("      500 ms release: the note sounds for %d samples after note off (expected about 24000)\n", tail);
    check (tail > 23900 && tail <= 24001, "the amp release fades a note out over its release time");
}

void testSustainChangeDuringNote()
{
    // Lowering the sustain to 50 % while a note is held should take effect on that note, gliding
    // to half its level rather than waiting for the next note.
    const auto output = render ({ .events = heldNote,
                                  .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                  {
                                      if (blockStart == volumeChangeSample)
                                          setParameter (p, "ampSustain", 50.0f);
                                  } });
    const auto before = peakBetween (output, volumeChangeSample - 4800, volumeChangeSample);
    const auto after = peakBetween (output, volumeChangeSample + 4800, (int) output.size());
    std::printf ("      sustain lowered mid-note: peak %.4f before, %.4f after\n", before, after);
    check (std::abs (before - 0.1f) < 0.001f && std::abs (after - 0.05f) < 0.001f,
           "changing the sustain during a note changes that note's level");
}

void testDefaultEnvelopeKeepsOldSound()
{
    // The defaults should reproduce the plug-in before the envelope existed: full level at once,
    // and the 5 ms (240 sample) fade on release that the other tests check.
    const auto byDefault = render ({ .events = { { 0, noteOn (69) }, { 4800, noteOff (69) } } });
    const auto explicitDefaults = render ({ .events = { { 0, noteOn (69) }, { 4800, noteOff (69) } },
                                            .setUp = withEnvelope (0.0f, 0.2f, 100.0f, 0.005f) });
    check (byDefault == explicitDefaults && std::abs (peakBetween (byDefault, 0, 200) - 0.1f) < 0.001f,
           "the default envelope plays notes at full level at once, with a 5 ms release");
}

// For the voice-stealing tests: middle C (261.63 Hz) on each of the 16 MIDI channels, so all 16
// voices are busy playing identical notes, whichever one gets stolen. Volume is lowered so all 16
// together stay below full scale.
//
// A 17th note arrives at sample 3715, when the notes are at the peak of a cycle (a quarter of the
// way through their 21st), the worst moment to cut one off.
constexpr int stealSample = 3715;

std::vector<TimedMessage> sixteenMiddleCs (int firstChannel = 1)
{
    std::vector<TimedMessage> events;

    for (int channel = firstChannel; channel <= 16; ++channel)
        events.push_back ({ 0, juce::MidiMessage::noteOn (channel, 60, (juce::uint8) 100) });

    return events;
}

const ProcessorSetUp quieter = [] (juce::AudioProcessor& p) { setParameter (p, "volume", -12.0f); };

std::vector<float> renderWithSteal()
{
    auto events = sixteenMiddleCs();
    events.push_back ({ stealSample, noteOn (64) });
    return render ({ .numSamples = 9600, .events = events, .setUp = quieter });
}

void testVoiceStealDoesNotClick()
{
    const auto output = renderWithSteal();
    const auto steady = largestStep (std::vector<float> (output.begin(), output.begin() + stealSample));
    const auto atSteal = largestStep (std::vector<float> (output.begin() + stealSample - 10, output.begin() + stealSample + 48));
    std::printf ("      largest sample-to-sample step: %.5f before the steal, %.5f around it\n", steady, atSteal);
    check (atSteal <= steady * 1.1f, "stealing a voice for a 17th note doesn't make the output jump");
}

void testStolenNoteFadesOut()
{
    // Subtracting a render where the stolen note was never played leaves just that note: it should
    // carry on at full level when stolen, then fade to nothing over 5 ms (240 samples).
    const auto withSteal = renderWithSteal();
    auto events = sixteenMiddleCs (2);
    events.push_back ({ stealSample, noteOn (64) });
    const auto withoutStolenNote = render ({ .numSamples = 9600, .events = events, .setUp = quieter });

    auto stolenNoteAt = [&] (int i) { return std::abs (withSteal[(size_t) i] - withoutStolenNote[(size_t) i]); };
    const auto peak = 0.1f * juce::Decibels::decibelsToGain (-12.0f);
    float afterFade = 0.0f;

    for (auto i = stealSample + 240; i < 9600; ++i)
        afterFade = std::max (afterFade, stolenNoteAt (i));

    // Halfway through the fade, the note should be at half what it would have been if never stolen.
    const auto unstolen = render ({ .numSamples = 9600, .events = { { 0, noteOn (60) } }, .setUp = quieter });
    const auto halfway = stealSample + 120;
    const auto fadeAtHalfway = stolenNoteAt (halfway) / std::abs (unstolen[(size_t) halfway]);

    std::printf ("      stolen note: %.4f when stolen (its peak is %.4f), at %.3f of its level halfway through the fade, "
                 "%.1e after it\n", stolenNoteAt (stealSample), peak, fadeAtHalfway, afterFade);
    check (std::abs (stolenNoteAt (stealSample) - peak) < 0.001f, "a stolen note carries on at its level when stolen");
    check (std::abs (fadeAtHalfway - 0.5f) < 0.01f, "2.5 ms later it's faded to half");
    check (afterFade < 1.0e-6f, "and it's silent 5 ms after it was stolen");
}

void testAllSoundOffFades()
{
    // MIDI "all sound off" (controller 120) is meant to silence everything at once. JUCE's synth
    // treats it like "all notes off", releasing every note through its envelope, so with the
    // default 5 ms release it fades rather than cutting off.
    const auto output = render ({ .numSamples = 9600,
                                  .events = { { 0, noteOn (60) }, { stealSample, controller (120, 0) } } });
    const auto steady = largestStep (std::vector<float> (output.begin(), output.begin() + stealSample));
    const auto atCut = largestStep (std::vector<float> (output.begin() + stealSample - 10, output.end()));
    const auto end = lastNonZeroIndex (output);
    check (atCut <= steady * 1.1f && end > stealSample && end <= stealSample + 240,
           "'all sound off' releases notes over the 5 ms release instead of cutting them");
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
    testDefaultWaveformIsSine();
    testWaveformsAreEquallyLoud();
    testWaveformChangeCrossfades();
    testNoteAfterWaveformChangeStartsDirectly();
    testAmpAttack();
    testAmpDecayAndSustain();
    testAmpRelease();
    testSustainChangeDuringNote();
    testDefaultEnvelopeKeepsOldSound();
    testVoiceStealDoesNotClick();
    testStolenNoteFadesOut();
    testAllSoundOffFades();
}

} // namespace testing
