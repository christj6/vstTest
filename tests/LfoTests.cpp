#include "TestHelpers.h"

// LFO tests: vibrato from the LFO, rendered through the whole plug-in.

namespace testing
{

namespace
{

ProcessorSetUp withLfo (float rateHz, float depthPercent)
{
    return [=] (juce::AudioProcessor& p)
    {
        setParameter (p, "lfoRate", rateHz);
        setParameter (p, "lfoDepth", depthPercent);
    };
}

// The frequency an A4 note should average between two sample positions, with an LFO of the given
// rate moving its pitch by up to `semitones` either way. The LFO starts at the beginning of its sine
// cycle (rising) at sample 0.
double expectedVibratoFrequency (double rateHz, double semitones, int from, int to)
{
    double sum = 0.0;

    for (int i = from; i < to; ++i)
        sum += 440.0 * std::exp2 (semitones * std::sin (juce::MathConstants<double>::twoPi * rateHz * i / testSampleRate) / 12.0);

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
}

} // namespace testing
