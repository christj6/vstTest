#include "TestHelpers.h"

// Control tests: Volume, velocity and pitch bend, rendered through the whole plug-in.

namespace testing
{

namespace
{

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

std::vector<float> renderVolumeDrop()
{
    return render ({ .numSamples = 48000,
                     .events = heldNote,
                     .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                     {
                         if (blockStart == changeSample)
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
    const auto glide = lastNonZeroIndex (output) - changeSample;
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
                                      if (blockStart == changeSample)
                                          setParameter (p, "velocitySensitivity", 100.0f);
                                  } });
    const auto afterChange = peakBetween (output, changeSample, (int) output.size());
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
                                  .events = { { 0, noteOn (69) }, { changeSample, pitchWheel (16383) } } });
    const auto firstPeriod = firstPeriodAfter (output, changeSample);
    const auto settled = frequencyBetween (output, changeSample + 960, changeSample + 960 + 9600, testSampleRate);
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


} // namespace

void runControlTests()
{
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
