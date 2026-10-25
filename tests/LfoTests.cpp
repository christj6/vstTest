#include "TestHelpers.h"

// LFO tests: the LFO moving the pitch (vibrato), the volume (tremolo) or the filter cutoff, rendered
// through the whole plug-in.

namespace testing
{

namespace
{

constexpr int pitch = 0, volume = 1, cutoff = 2;

ProcessorSetUp withLfo (float rateHz, float depthPercent, int destination = pitch)
{
    return [=] (juce::AudioProcessor& p)
    {
        setParameter (p, "lfoRate", rateHz);
        setParameter (p, "lfoDepth", depthPercent);
        setParameter (p, "lfoDestination", (float) destination);
    };
}

// The LFO's value at a sample: a sine starting at the beginning of its cycle (rising) at sample 0.
double lfoAt (double rateHz, int sample)
{
    return std::sin (juce::MathConstants<double>::twoPi * rateHz * sample / testSampleRate);
}

// The frequency an A4 note should average between two sample positions, with an LFO of the given
// rate moving its pitch by up to `semitones` either way. The LFO starts at the beginning of its sine
// cycle (rising) at sample 0.
double expectedVibratoFrequency (double rateHz, double semitones, int from, int to)
{
    double sum = 0.0;

    for (int i = from; i < to; ++i)
        sum += 440.0 * std::exp2 (semitones * lfoAt (rateHz, i) / 12.0);

    return sum / (to - from);
}

// Checks an A4 note's frequency against the expected vibrato over 20 ms windows centred on each of
// `centres` (in seconds). Returns the largest difference, as a fraction of the expected frequency.
double worstVibratoError (const std::vector<float>& output, double rateHz, double semitones, std::initializer_list<double> centres)
{
    double worst = 0.0;

    for (auto centre : centres)
    {
        const auto from = (int) (centre * testSampleRate) - 480;
        const auto to = from + 960;
        const auto measured = frequencyBetween (output, from, to, testSampleRate);
        const auto expected = expectedVibratoFrequency (rateHz, semitones, from, to);
        std::printf ("      at %.3f s: %.2f Hz (expected %.2f)\n", centre, measured, expected);
        worst = std::max (worst, std::abs (measured / expected - 1.0));
    }

    return worst;
}

void testLfoHasNoEffectByDefault()
{
    // At its default depth of 0 %, the LFO's rate makes no difference at all.
    const auto byDefault = render ({ .events = heldNote });
    const auto otherRate = render ({ .events = heldNote, .setUp = [] (juce::AudioProcessor& p) { setParameter (p, "lfoRate", 0.3f); } });
    check (byDefault == otherRate, "with the LFO depth at 0 %, the LFO has no effect");
}

void testVibratoDepth()
{
    // At 50 % depth the pitch moves 12 × 0.5² = 3 semitones either way: at 1 Hz, A4 rises to C5
    // (523.25 Hz) a quarter of a second in, is back at 440 Hz at half a second, and falls to F#4
    // (369.99 Hz) at three quarters.
    const auto output = render ({ .events = heldNote, .setUp = withLfo (1.0f, 50.0f) });
    const auto worst = worstVibratoError (output, 1.0, 3.0, { 0.25, 0.5, 0.75 });
    check (worst < 0.002, "at 50 % depth the LFO moves the pitch 3 semitones either way");
}

void testVibratoDepthIsSquared()
{
    // At 20 % the pitch moves 12 × 0.2² = 0.48 semitones either way: a typical vibrato.
    const auto output = render ({ .events = heldNote, .setUp = withLfo (1.0f, 20.0f) });
    const auto worst = worstVibratoError (output, 1.0, 0.48, { 0.25, 0.75 });
    check (worst < 0.001, "at 20 % depth the LFO moves the pitch about half a semitone either way");
}

void testVibratoRate()
{
    // At 4 Hz the pitch peaks every quarter of a second, starting at 1/16 s.
    const auto output = render ({ .events = heldNote, .setUp = withLfo (4.0f, 50.0f) });
    const auto worst = worstVibratoError (output, 4.0, 3.0, { 0.5625, 0.6875, 0.8125 });
    check (worst < 0.003, "the LFO rate sets how fast the pitch moves");
}

void testLfoIsSharedAndFreeRunning()
{
    // The LFO keeps running whether or not notes are playing, and every note hears the same one. A
    // note started a quarter of a second in, at 1 Hz, starts at the top of the vibrato, not the middle.
    const auto output = render ({ .events = { { 12000, noteOn (69) } }, .setUp = withLfo (1.0f, 50.0f) });
    const auto worst = worstVibratoError (output, 1.0, 3.0, { 0.26, 0.5 });
    check (worst < 0.002, "a note joins the LFO wherever it has got to");
}

void testLfoDepthGlides()
{
    // The LFO's rate is set so it's at the top of its cycle exactly when the depth jumps from 0 to
    // 100 %. Without a glide the pitch would leap an octave at once; with it, the depth takes 20 ms.
    const auto rateHz = (float) (0.25 * testSampleRate / changeSample);
    const auto output = render ({ .events = heldNote,
                                  .setUp = withLfo (rateHz, 0.0f),
                                  .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                  {
                                      if (blockStart == changeSample)
                                          setParameter (p, "lfoDepth", 100.0f);
                                  } });
    const auto firstFiveMs = frequencyBetween (output, changeSample, changeSample + 240, testSampleRate);
    const auto afterGlide = frequencyBetween (output, changeSample + 960, changeSample + 1920, testSampleRate);
    std::printf ("      depth 0 -> 100 %% at the LFO's peak: %.1f Hz in the first 5 ms, %.1f Hz after 20 ms (expected about 880)\n",
                 firstFiveMs, afterGlide);
    check (firstFiveMs < 460.0 && afterGlide > 870.0, "a change of LFO depth glides instead of jumping");
}

void testLfoBlockSizeIndependence()
{
    // The LFO is worked out in pieces of at most 512 samples, so blocks of 2048 are split up too.
    const std::vector<TimedMessage> events { { 3, noteOn (60) },
                                             { 1000, noteOn (64) },
                                             { 3000, juce::MidiMessage::pitchWheel (1, 12000) },
                                             { 5000, noteOff (60) } };
    const auto setUp = withLfo (7.0f, 40.0f);
    const auto blocksOf1 = render ({ .blockSize = 1, .numSamples = 8000, .events = events, .setUp = setUp });
    const auto blocksOf7 = render ({ .blockSize = 7, .numSamples = 8000, .events = events, .setUp = setUp });
    const auto blocksOf512 = render ({ .blockSize = 512, .numSamples = 8000, .events = events, .setUp = setUp });
    const auto blocksOf2048 = render ({ .blockSize = 2048, .numSamples = 8000, .events = events, .setUp = setUp });
    check (blocksOf1 == blocksOf7 && blocksOf7 == blocksOf512 && blocksOf512 == blocksOf2048,
           "with the LFO on, output is identical with block sizes 1, 7, 512 and 2048");
}

//==============================================================================
void testDefaultDestinationIsPitch()
{
    const auto byDefault = render ({ .events = heldNote,
                                     .setUp = [] (juce::AudioProcessor& p)
                                     {
                                         setParameter (p, "lfoRate", 1.0f);
                                         setParameter (p, "lfoDepth", 50.0f);
                                     } });
    const auto chosenPitch = render ({ .events = heldNote, .setUp = withLfo (1.0f, 50.0f, pitch) });
    check (byDefault == chosenPitch, "the LFO's destination defaults to the pitch");
}

// The largest difference, over a whole second, between a tremolo note and the plain note scaled by
// the level the LFO should give it: full at the LFO's peak, down to 1 - depth at its trough.
float worstTremoloError (float depthPercent)
{
    const auto plain = render ({ .events = heldNote });
    const auto tremolo = render ({ .events = heldNote, .setUp = withLfo (1.0f, depthPercent, volume) });
    const auto depth = depthPercent / 100.0;
    float worst = 0.0f;

    for (int i = 0; i < (int) plain.size(); ++i)
    {
        const auto gain = 1.0 - depth * (1.0 - lfoAt (1.0, i)) / 2.0;
        worst = std::max (worst, std::abs (tremolo[(size_t) i] - (float) (gain * plain[(size_t) i])));
    }

    std::printf ("      %.0f %% tremolo: at most %.1e from the expected level\n", depthPercent, worst);
    return worst;
}

void testTremolo()
{
    // The LFO reads a wave table, within about 1.2e-6 of a true sine, so the note is too.
    check (worstTremoloError (100.0f) < 1.0e-6f && worstTremoloError (50.0f) < 1.0e-6f,
           "sent to the volume, the LFO swings the level between full and 1 - depth, sample by sample");
}

// How much the low-pass filter at 440 Hz turns down an A4 sine within 10 ms of `centre` (in
// seconds), with the LFO at 1 Hz sent to the cutoff at the given depth, in dB.
double cutoffLfoGainAt (const std::vector<float>& filtered, const std::vector<float>& unfiltered, double centre)
{
    const auto from = (int) (centre * testSampleRate) - 480;
    return toDecibels (rmsBetween (filtered, from, from + 960) / rmsBetween (unfiltered, from, from + 960));
}

void testLfoOnCutoff()
{
    // At 50 % the cutoff moves 2 octaves either way from 440 Hz: up to 1760 Hz at a quarter of a
    // second, where A4 passes almost untouched; through 440 Hz at half a second (3 dB down); and
    // down to 110 Hz at three quarters, two octaves below A4 (about 24 dB down).
    const auto unfiltered = render ({ .events = heldNote });
    const auto filtered = render ({ .events = heldNote,
                                    .setUp = [] (juce::AudioProcessor& p)
                                    {
                                        setParameter (p, "filterEnabled", 1.0f);
                                        setParameter (p, "filterCutoff", 440.0f);
                                        withLfo (1.0f, 50.0f, cutoff) (p);
                                    } });
    const auto top = cutoffLfoGainAt (filtered, unfiltered, 0.25);
    const auto middle = cutoffLfoGainAt (filtered, unfiltered, 0.5);
    const auto bottom = cutoffLfoGainAt (filtered, unfiltered, 0.75);
    std::printf ("      cutoff LFO: %+.2f dB at its top, %+.2f dB in the middle, %+.2f dB at its bottom "
                 "(expected about -0.02, -3.01, -24.1)\n", top, middle, bottom);
    check (std::abs (top + 0.02) < 0.2 && std::abs (middle + 3.01) < 0.5 && std::abs (bottom + 24.1) < 1.0,
           "sent to the filter cutoff, the LFO moves it 4 × depth octaves either way");
}

void testCutoffLfoNeedsTheFilter()
{
    // With the filter off there's no cutoff to move, so the LFO does nothing.
    const auto plain = render ({ .events = heldNote });
    const auto withCutoffLfo = render ({ .events = heldNote, .setUp = withLfo (3.0f, 100.0f, cutoff) });
    check (plain == withCutoffLfo, "sent to the filter cutoff with the filter off, the LFO has no effect");
}

void testDestinationChangeCrossfades()
{
    // At full depth on the pitch, with the LFO at the bottom of its cycle (an octave down), switch it
    // to the volume, where the bottom of its cycle is silence. Switching at once would drop the note
    // to nothing in one sample; crossfading over 10 ms, it stays smooth.
    const auto rateHz = (float) (0.75 * testSampleRate / changeSample);
    const auto output = render ({ .events = heldNote,
                                  .setUp = withLfo (rateHz, 100.0f, pitch),
                                  .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                  {
                                      if (blockStart == changeSample)
                                          setParameter (p, "lfoDestination", (float) volume);
                                  } });
    const auto steady = largestStep (std::vector<float> (output.begin() + changeSample - 960, output.begin() + changeSample));
    const auto atChange = largestStep (std::vector<float> (output.begin() + changeSample - 10, output.begin() + changeSample + 960));
    std::printf ("      largest step: %.4f before switching pitch to volume, %.4f around the switch\n", steady, atChange);
    check (atChange <= steady * 1.1f, "changing the LFO's destination during a note crossfades instead of jumping");
}

void testCutoffLfoBlockSizeIndependence()
{
    // The LFO moving the cutoff, then its depth turned to 0 at sample 14336, where blocks of 1, 7, 512
    // and 2048 all start. The depth glides to 0 partway through a block, and the filter has to land on
    // its final setting on that very sample, whatever the block size.
    constexpr int stopSample = 7 * 2048;
    const std::vector<TimedMessage> events { { 3, noteOn (45) }, { 1000, noteOn (52) } };
    const auto setUp = [] (juce::AudioProcessor& p)
    {
        setParameter (p, "waveform", 2.0f);
        setParameter (p, "filterEnabled", 1.0f);
        setParameter (p, "filterCutoff", 600.0f);
        setParameter (p, "filterResonance", 50.0f);
        withLfo (3.0f, 60.0f, cutoff) (p);
    };
    const auto stopLfo = [] (juce::AudioProcessor& p, int blockStart)
    {
        if (blockStart == stopSample)
            setParameter (p, "lfoDepth", 0.0f);
    };
    auto renderInBlocksOf = [&] (int blockSize)
    {
        return render ({ .blockSize = blockSize, .numSamples = 20000, .events = events, .setUp = setUp, .beforeEachBlock = stopLfo });
    };
    const auto blocksOf1 = renderInBlocksOf (1);
    const auto blocksOf7 = renderInBlocksOf (7);
    const auto blocksOf512 = renderInBlocksOf (512);
    const auto blocksOf2048 = renderInBlocksOf (2048);
    check (blocksOf1 == blocksOf7 && blocksOf7 == blocksOf512 && blocksOf512 == blocksOf2048,
           "with the LFO moving the cutoff and then stopping, output is identical with block sizes 1, 7, 512 and 2048");
}

} // namespace

void runLfoTests()
{
    testLfoHasNoEffectByDefault();
    testVibratoDepth();
    testVibratoDepthIsSquared();
    testVibratoRate();
    testLfoIsSharedAndFreeRunning();
    testLfoDepthGlides();
    testLfoBlockSizeIndependence();
    testDefaultDestinationIsPitch();
    testTremolo();
    testLfoOnCutoff();
    testCutoffLfoNeedsTheFilter();
    testDestinationChangeCrossfades();
    testCutoffLfoBlockSizeIndependence();
}

} // namespace testing
