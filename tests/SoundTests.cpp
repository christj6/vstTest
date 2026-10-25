#include "TestHelpers.h"

// Sound tests: the waveforms, the amp envelope and the filter, rendered through the whole plug-in.

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

//==============================================================================
ProcessorSetUp withFilter (float cutoffHz, float resonancePercent, int waveform = sine)
{
    return [=] (juce::AudioProcessor& p)
    {
        setParameter (p, "waveform", (float) waveform);
        setParameter (p, "filterEnabled", 1.0f);
        setParameter (p, "filterCutoff", cutoffHz);
        setParameter (p, "filterResonance", resonancePercent);
    };
}

// The magnitude of each frequency in `length` samples from `start`, with a Hann window so that
// strong frequencies don't spill into weak ones.
std::vector<double> windowedSpectrum (const std::vector<float>& samples, int start, int length)
{
    std::vector<float> windowed ((size_t) length);

    for (int i = 0; i < length; ++i)
        windowed[(size_t) i] = samples[(size_t) (start + i)]
                             * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / length));

    return harmonicsOf (windowed);
}

void testFilterOffByDefault()
{
    const auto byDefault = render ({ .events = heldNote });
    const auto explicitlyOff = render ({ .events = heldNote, .setUp = [] (juce::AudioProcessor& p) { setParameter (p, "filterEnabled", 0.0f); } });
    check (byDefault == explicitlyOff, "the filter is off by default");
}

void testFilterCutsHighHarmonics()
{
    // A 110 Hz saw through a 1 kHz cutoff, harmonic by harmonic, compared with the same note unfiltered:
    // the 2nd harmonic (220 Hz) untouched, the 9th (990 Hz) 3 dB down, the 36th (3960 Hz) about 24 dB down.
    const std::vector<TimedMessage> lowNote { { 0, noteOn (45) } };
    const auto unfiltered = render ({ .events = lowNote, .setUp = withWaveform (saw) });
    const auto filtered = render ({ .events = lowNote, .setUp = withFilter (1000.0f, 0.0f, saw) });

    constexpr int length = 32768;
    const auto before = windowedSpectrum (unfiltered, 9600, length);
    const auto after = windowedSpectrum (filtered, 9600, length);
    auto changeAt = [&] (int harmonic)
    {
        const auto bin = (size_t) juce::roundToInt (harmonic * 110.0 * length / testSampleRate);
        return toDecibels (after[bin] / before[bin]);
    };

    std::printf ("      110 Hz saw, 1 kHz cutoff: harmonic 2 %+.2f dB, 9 %+.2f dB, 36 %+.2f dB (expected about 0, -3, -24)\n",
                 changeAt (2), changeAt (9), changeAt (36));
    check (std::abs (changeAt (2)) < 0.1 && std::abs (changeAt (9) + 3.0) < 0.5 && std::abs (changeAt (36) + 24.0) < 1.0,
           "the filter keeps harmonics below its cutoff and removes those above it");
}

void testOpenFilterBarelyChanges()
{
    // Switched on at its default, fully open, the filter should make almost no difference.
    const std::vector<TimedMessage> lowNote { { 0, noteOn (45) } };
    const auto unfiltered = rmsBetween (render ({ .events = lowNote, .setUp = withWaveform (saw) }), 4800, 48000);
    const auto openFilter = rmsBetween (render ({ .events = lowNote, .setUp = withFilter (20000.0f, 0.0f, saw) }), 4800, 48000);
    std::printf ("      saw through the fully open filter: %+.3f dB\n", toDecibels (openFilter / unfiltered));
    check (std::abs (toDecibels (openFilter / unfiltered)) < 0.05, "switching the filter on at its defaults barely changes the sound");
}

void testCutoffChangeGlides()
{
    // A 220 Hz sine with the cutoff dropped from 20 kHz to 100 Hz mid-note. The level should glide
    // down over about 20 ms, not drop at once.
    const std::vector<TimedMessage> note { { 0, noteOn (57) } };
    const auto output = render ({ .events = note,
                                  .setUp = withFilter (20000.0f, 0.0f),
                                  .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                  {
                                      if (blockStart == changeSample)
                                          setParameter (p, "filterCutoff", 100.0f);
                                  } });
    const auto early = peakBetween (output, changeSample + 240, changeSample + 480);
    const auto settled = peakBetween (output, changeSample + 9600, (int) output.size());
    std::printf ("      cutoff dropped mid-note: peak %.4f 5-10 ms later, settling at %.4f (expected about 0.0204)\n", early, settled);
    check (early > 2.0f * settled && std::abs (settled - 0.0204f) < 0.003f, "a cutoff change glides rather than jumping");
}

void testFilterSwitchCrossfades()
{
    // A 2 kHz sine; the filter (at a 200 Hz cutoff) is switched on mid-note, and in another render
    // switched off. Either way the output shouldn't jump.
    const std::vector<TimedMessage> note { { 0, noteOn (96) } };
    auto switchAtChange = [] (float enabled)
    {
        return [enabled] (juce::AudioProcessor& p, int blockStart)
        {
            if (blockStart == changeSample)
                setParameter (p, "filterEnabled", enabled);
        };
    };

    const auto switchedOn = render ({ .events = note,
                                      .setUp = [] (juce::AudioProcessor& p) { setParameter (p, "filterCutoff", 200.0f); },
                                      .beforeEachBlock = switchAtChange (1.0f) });
    const auto switchedOff = render ({ .events = note, .setUp = withFilter (200.0f, 0.0f), .beforeEachBlock = switchAtChange (0.0f) });

    auto stepNear = [] (const std::vector<float>& output)
    {
        return largestStep (std::vector<float> (output.begin() + changeSample - 10, output.begin() + changeSample + 600));
    };

    const auto unfilteredStep = largestStep (std::vector<float> (switchedOn.begin(), switchedOn.begin() + changeSample));
    std::printf ("      largest step: %.4f unfiltered, %.4f switching on, %.4f switching off\n",
                 unfilteredStep, stepNear (switchedOn), stepNear (switchedOff));
    check (stepNear (switchedOn) <= unfilteredStep * 1.1f && stepNear (switchedOff) <= unfilteredStep * 1.1f,
           "switching the filter on or off during a note doesn't jump");
    check (peakBetween (switchedOn, changeSample + 2400, (int) switchedOn.size()) < 0.01f,
           "once switched on, the filter takes effect");
}

void testEachNoteStartsWithCleanFilter()
{
    // A ringing filter (100 Hz, full resonance). The first note ends, and a second starts on the same
    // voice soon after. It should sound exactly as if it were the only note ever played.
    constexpr int secondNoteStart = 3120;
    const auto setUp = withFilter (100.0f, 100.0f, saw);
    const auto twoNotes = render ({ .numSamples = 9600,
                                    .events = { { 0, noteOn (45) }, { 2400, noteOff (45) }, { secondNoteStart, noteOn (57) } },
                                    .setUp = setUp });
    const auto secondAlone = render ({ .numSamples = 9600, .events = { { secondNoteStart, noteOn (57) } }, .setUp = setUp });
    check (std::equal (twoNotes.begin() + secondNoteStart, twoNotes.end(), secondAlone.begin() + secondNoteStart),
           "every note starts with a clean filter, whatever the voice played before");
}

void testFilterBlockSizeIndependence()
{
    const std::vector<TimedMessage> events { { 3, noteOn (48) },
                                             { 700, noteOn (55) },
                                             { 2000, juce::MidiMessage::pitchWheel (1, 13000) },
                                             { 4000, noteOff (48) } };
    const auto setUp = withFilter (800.0f, 50.0f, square);
    const auto blocksOf1 = render ({ .blockSize = 1, .numSamples = 8000, .events = events, .setUp = setUp });
    const auto blocksOf7 = render ({ .blockSize = 7, .numSamples = 8000, .events = events, .setUp = setUp });
    const auto blocksOf512 = render ({ .numSamples = 8000, .events = events, .setUp = setUp });
    check (blocksOf1 == blocksOf7 && blocksOf7 == blocksOf512, "with the filter on, output is identical with block sizes 1, 7 and 512");
}

void testHighResonanceSweepStaysFinite()
{
    // A saw chord at full resonance, the cutoff jumping between 20 Hz and 20 kHz on every block.
    // Nothing should blow up: the output stays finite and keeps sounding.
    const auto output = render ({ .events = { { 0, noteOn (40) }, { 0, noteOn (52) }, { 0, noteOn (64) } },
                                  .setUp = withFilter (1000.0f, 100.0f, saw),
                                  .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                  {
                                      setParameter (p, "filterCutoff", (blockStart / 512) % 2 == 0 ? 20.0f : 20000.0f);
                                  } });
    bool allFinite = true;

    for (auto s : output)
        allFinite = allFinite && std::isfinite (s);

    check (allFinite && peakBetween (output, 24000, 48000) > 0.01f, "full resonance with a wildly moving cutoff stays stable");
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
    testFilterOffByDefault();
    testFilterCutsHighHarmonics();
    testOpenFilterBarelyChanges();
    testCutoffChangeGlides();
    testFilterSwitchCrossfades();
    testEachNoteStartsWithCleanFilter();
    testFilterBlockSizeIndependence();
    testHighResonanceSweepStaysFinite();
}

} // namespace testing
