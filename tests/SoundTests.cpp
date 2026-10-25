#include "TestHelpers.h"

// Sound tests: the waveforms and the amp envelope, rendered through the whole plug-in.

namespace testing
{

namespace
{

constexpr int sine = 0, saw = 2, square = 3;

ProcessorSetUp withWaveform (int waveform)
{
    return [waveform] (juce::AudioProcessor& p) { setParameter (p, "waveform", (float) waveform); };
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
                                        if (blockStart == changeSample)
                                            setParameter (p, "waveform", (float) square);
                                    } });

    constexpr int fadeLength = 480;
    const auto halfway = changeSample + fadeLength / 2 - 1;
    const auto blendAtHalfway = 0.5f * (sawOnly[(size_t) halfway] + squareOnly[(size_t) halfway]);

    check (std::equal (switched.begin(), switched.begin() + changeSample, sawOnly.begin()),
           "before a waveform change, the note is unchanged");
    check (std::abs (switched[(size_t) halfway] - blendAtHalfway) < 1.0e-6f,
           "halfway through the 10 ms crossfade, the note is an even blend of both waveforms");
    check (std::equal (switched.begin() + changeSample + fadeLength - 1, switched.end(),
                       squareOnly.begin() + changeSample + fadeLength - 1),
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
                                      if (blockStart == changeSample)
                                          setParameter (p, "ampSustain", 50.0f);
                                  } });
    const auto before = peakBetween (output, changeSample - 4800, changeSample);
    const auto after = peakBetween (output, changeSample + 4800, (int) output.size());
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

} // namespace

void runSoundTests()
{
    testDefaultWaveformIsSine();
    testWaveformsAreEquallyLoud();
    testWaveformChangeCrossfades();
    testNoteAfterWaveformChangeStartsDirectly();
    testAmpAttack();
    testAmpDecayAndSustain();
    testAmpRelease();
    testSustainChangeDuringNote();
    testDefaultEnvelopeKeepsOldSound();
}

} // namespace testing
