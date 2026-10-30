#include "TestHelpers.h"

#include "Synth.h"
#include "Waveforms.h"

// LFO tests: the LFO's shapes moving the pitch (vibrato), the volume (tremolo) or the filter cutoff,
// rendered through the whole plug-in; and the Lfo class on its own.

namespace testing
{

namespace
{

constexpr int pitch = 0, volume = 1, cutoff = 2;
constexpr int sine = 0, triangle = 1, saw = 2, square = 3;

ProcessorSetUp withLfo (float rateHz, float depthPercent, int destination = pitch, int shape = sine)
{
    return [=] (juce::AudioProcessor& p)
    {
        setParameter (p, "lfoRate", rateHz);
        setParameter (p, "lfoDepth", depthPercent);
        setParameter (p, "lfoDestination", (float) destination);
        setParameter (p, "lfoShape", (float) shape);
    };
}

// The LFO's value at a sample, starting at the beginning of its cycle at sample 0: the ideal shape,
// with instant jumps where the real LFO's are a few milliseconds long.
double lfoAt (double rateHz, int sample, int shape = sine)
{
    const auto phase = std::fmod (juce::MathConstants<double>::twoPi * rateHz * sample / testSampleRate, juce::MathConstants<double>::twoPi);
    return shape == sine ? std::sin (phase) : boringsynth::waveformShape (shape, phase);
}

// The frequency an A4 note should average between two sample positions, with an LFO of the given
// rate and shape moving its pitch by up to `semitones` either way.
double expectedVibratoFrequency (double rateHz, double semitones, int from, int to, int shape)
{
    double sum = 0.0;

    for (int i = from; i < to; ++i)
        sum += 440.0 * std::exp2 (semitones * lfoAt (rateHz, i, shape) / 12.0);

    return sum / (to - from);
}

// Checks an A4 note's frequency against the expected vibrato over 20 ms windows centred on each of
// `centres` (in seconds). Returns the largest difference, as a fraction of the expected frequency.
double worstVibratoError (const std::vector<float>& output, double rateHz, double semitones, std::initializer_list<double> centres,
                          int shape = sine)
{
    double worst = 0.0;

    for (auto centre : centres)
    {
        const auto from = (int) (centre * testSampleRate) - 480;
        const auto to = from + 960;
        const auto measured = frequencyBetween (output, from, to, testSampleRate);
        const auto expected = expectedVibratoFrequency (rateHz, semitones, from, to, shape);
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
        withLfo (3.0f, 60.0f, cutoff, square) (p);
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
           "with a square LFO moving the cutoff and then stopping, output is identical with block sizes 1, 7, 512 and 2048");
}

//==============================================================================
void testDefaultShapeIsSine()
{
    const auto byDefault = render ({ .events = heldNote,
                                     .setUp = [] (juce::AudioProcessor& p)
                                     {
                                         setParameter (p, "lfoRate", 1.0f);
                                         setParameter (p, "lfoDepth", 50.0f);
                                     } });
    const auto chosenSine = render ({ .events = heldNote, .setUp = withLfo (1.0f, 50.0f, pitch, sine) });
    check (byDefault == chosenSine, "the LFO's shape defaults to the sine");
}

void testLfoShapesOnPitch()
{
    // At 50 % depth on the pitch (3 semitones either way) and 1 Hz, each shape should move the pitch
    // as the shape itself does, measured away from its corners and jumps: the triangle halfway up and
    // down its slopes, the saw halfway up its ramp either side of its jump, the square on its flat
    // tops (where a sine would only touch the top and bottom).
    const struct { int shape; const char* name; std::initializer_list<double> centres; } cases[] {
        { triangle, "triangle", { 0.125, 0.375, 0.625 } },
        { saw, "saw", { 0.25, 0.75 } },
        { square, "square", { 0.1, 0.25, 0.4, 0.6, 0.75, 0.9 } } };
    double worst = 0.0;

    for (const auto& c : cases)
    {
        std::printf ("      %s:\n", c.name);
        const auto output = render ({ .events = heldNote, .setUp = withLfo (1.0f, 50.0f, pitch, c.shape) });
        worst = std::max (worst, worstVibratoError (output, 1.0, 3.0, c.centres, c.shape));
    }

    check (worst < 0.003, "the triangle, saw and square LFOs move the pitch in their own shapes");
}

void testShapeChangeCrossfades()
{
    // Tremolo at full depth, switched from sine to square just after the middle of the LFO's cycle,
    // where the sine is barely below 0 (a little over half level) and the square is at -1 (silence).
    // Switching at once would drop the note to nothing in one sample; crossfading over 10 ms, it
    // stays smooth.
    const auto rateHz = (float) (0.525 * testSampleRate / changeSample);
    const auto output = render ({ .events = heldNote,
                                  .setUp = withLfo (rateHz, 100.0f, volume, sine),
                                  .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                  {
                                      if (blockStart == changeSample)
                                          setParameter (p, "lfoShape", (float) square);
                                  } });
    const auto steady = largestStep (std::vector<float> (output.begin() + changeSample - 960, output.begin() + changeSample));
    const auto atChange = largestStep (std::vector<float> (output.begin() + changeSample - 10, output.begin() + changeSample + 960));
    std::printf ("      largest step: %.4f before switching sine to square, %.4f around the switch\n", steady, atChange);
    check (atChange <= steady * 1.5f, "changing the LFO's shape during a note crossfades instead of jumping");
}

void testSquareTremoloDoesntClick()
{
    // A full-depth square tremolo, fast: each jump between full level and silence is a 5 ms ramp, so
    // the output moves on each sample by hardly more than the plain note does. An instant jump would
    // move it by up to the note's whole level. 17.3 Hz doesn't divide A4's 440 Hz evenly, so the
    // jumps land all over the note's cycle; at 20 Hz every one would land where the note is at 0, and
    // even an instant jump would barely show.
    const auto plain = render ({ .events = heldNote });
    const auto tremolo = render ({ .events = heldNote, .setUp = withLfo (17.3f, 100.0f, volume, square) });
    const auto plainStep = largestStep (plain);
    const auto tremoloStep = largestStep (tremolo);
    std::printf ("      largest step: %.4f for the plain note, %.4f with a square tremolo at 17.3 Hz\n", plainStep, tremoloStep);
    check (tremoloStep <= plainStep * 1.2f, "a square LFO on the volume doesn't click");
}

//==============================================================================
// The mod wheel, adding to the LFO's depth.

constexpr int modWheel = 1, resetAllControllers = 121;

ProcessorSetUp withModWheelLfo (float depthPercent, float modWheelPercent)
{
    return [=] (juce::AudioProcessor& p)
    {
        withLfo (1.0f, depthPercent) (p);
        setParameter (p, "modWheelLfoDepth", modWheelPercent);
    };
}

// An A4 note with the mod wheel moved to `wheel` (0 to 127) just before it starts.
std::vector<TimedMessage> noteWithModWheelAt (int wheel, int channel = 1)
{
    return { { 0, controller (modWheel, wheel, channel) }, { 0, noteOn (69) } };
}

void testModWheelIgnoredByDefault()
{
    // At its default of 0 %, moving the mod wheel makes no difference at all.
    const std::vector<TimedMessage> wheelMoves { { 0, noteOn (69) },
                                                 { 5000, controller (modWheel, 127) },
                                                 { 20000, controller (modWheel, 64) } };
    const auto withoutWheel = render ({ .events = heldNote, .setUp = withLfo (5.0f, 30.0f) });
    const auto withWheel = render ({ .events = wheelMoves, .setUp = withLfo (5.0f, 30.0f) });
    check (withoutWheel == withWheel, "with Mod Wheel LFO Depth at 0 %, the mod wheel has no effect");
}

void testModWheelAddsDepth()
{
    // With the wheel fully up, its amount is added to the depth knob: 50 % on its own, or 30 % on top
    // of the knob's 20 %, both give 50 %, which moves the pitch 3 semitones either way. Halfway up
    // (64 / 127) at 100 %, the depth is 64 / 127 too. Measured once the depth has glided there.
    const auto halfWheel = 64.0f / 127.0f;
    const auto wheelOnly = render ({ .events = noteWithModWheelAt (127), .setUp = withModWheelLfo (0.0f, 50.0f) });
    const auto knobAndWheel = render ({ .events = noteWithModWheelAt (127), .setUp = withModWheelLfo (20.0f, 30.0f) });
    const auto wheelHalfway = render ({ .events = noteWithModWheelAt (64), .setUp = withModWheelLfo (0.0f, 100.0f) });
    std::printf ("      wheel fully up, adding 50 %%:\n");
    auto worst = worstVibratoError (wheelOnly, 1.0, 3.0, { 0.25, 0.75 });
    std::printf ("      knob at 20 %%, wheel fully up adding 30 %%:\n");
    worst = std::max (worst, worstVibratoError (knobAndWheel, 1.0, 3.0, { 0.25, 0.75 }));
    std::printf ("      wheel halfway, adding 100 %%:\n");
    worst = std::max (worst, worstVibratoError (wheelHalfway, 1.0, 12.0 * halfWheel * halfWheel, { 0.25, 0.75 }));
    check (worst < 0.002, "the mod wheel adds its amount, scaled by how far it's moved, to the LFO's depth");
}

void testModWheelDepthStopsAtFull()
{
    // The knob at 80 % plus the wheel's 100 % would be 180 %; the depth stops at 100 %, an octave
    // either way.
    const auto output = render ({ .events = noteWithModWheelAt (127), .setUp = withModWheelLfo (80.0f, 100.0f) });
    const auto worst = worstVibratoError (output, 1.0, 12.0, { 0.25, 0.75 });
    check (worst < 0.003, "the knob and the mod wheel together never take the depth past 100 %");
}

void testModWheelGlides()
{
    // The LFO's rate is set so it's at the top of its cycle when the wheel is pushed fully up, 100
    // samples into a block, adding 100 %. The pitch should stay put until that very sample, then glide
    // up over 20 ms rather than leaping an octave, and do so the same whatever the block size.
    const auto wheelSample = changeSample + 100;
    const auto rateHz = (float) (0.25 * testSampleRate / wheelSample);
    const std::vector<TimedMessage> events { { 0, noteOn (69) }, { wheelSample, controller (modWheel, 127) } };
    const auto setUp = [=] (juce::AudioProcessor& p)
    {
        withLfo (rateHz, 0.0f) (p);
        setParameter (p, "modWheelLfoDepth", 100.0f);
    };
    const auto plain = render ({ .events = heldNote });
    const auto output = render ({ .events = events, .setUp = setUp });
    const auto untilWheel = std::equal (plain.begin(), plain.begin() + wheelSample + 1, output.begin());
    const auto firstFiveMs = frequencyBetween (output, wheelSample, wheelSample + 240, testSampleRate);
    const auto afterGlide = frequencyBetween (output, wheelSample + 960, wheelSample + 1920, testSampleRate);
    std::printf ("      wheel pushed up at the LFO's peak: %.1f Hz in the first 5 ms, %.1f Hz after 20 ms (expected about 880)\n",
                 firstFiveMs, afterGlide);
    check (untilWheel && firstFiveMs < 460.0 && afterGlide > 870.0,
           "a mod wheel move takes effect from its own sample, gliding rather than jumping");

    auto renderInBlocksOf = [&] (int blockSize)
    {
        return render ({ .blockSize = blockSize, .numSamples = 30000, .events = events, .setUp = setUp });
    };
    const auto blocksOf1 = renderInBlocksOf (1);
    check (blocksOf1 == renderInBlocksOf (7) && blocksOf1 == renderInBlocksOf (512) && blocksOf1 == renderInBlocksOf (2048),
           "with the mod wheel moving, output is identical with block sizes 1, 7, 512 and 2048");
}

void testResetAllControllersDropsTheWheel()
{
    // MIDI "reset all controllers" puts the wheel back down: with the wheel the only depth, the
    // vibrato stops. Half a second later the note is back at 440 Hz where the vibrato would be at its
    // lowest.
    const std::vector<TimedMessage> events { { 0, controller (modWheel, 127) },
                                             { 0, noteOn (69) },
                                             { 12000, controller (resetAllControllers, 0) } };
    const auto output = render ({ .events = events, .setUp = withModWheelLfo (0.0f, 50.0f) });
    const auto atTrough = frequencyBetween (output, 36000 - 480, 36000 + 480, testSampleRate);
    std::printf ("      wheel reset at 0.25 s: %.2f Hz at 0.75 s (370 Hz if the wheel were still up)\n", atTrough);
    check (std::abs (atTrough - 440.0) < 0.5, "'reset all controllers' puts the mod wheel back down");
}

void testModWheelOnAnyChannel()
{
    // There's one LFO for every note, so the wheel counts whichever MIDI channel it comes on.
    const auto channel1 = render ({ .events = noteWithModWheelAt (127, 1), .setUp = withModWheelLfo (0.0f, 50.0f) });
    const auto channel2 = render ({ .events = noteWithModWheelAt (127, 2), .setUp = withModWheelLfo (0.0f, 50.0f) });
    check (channel1 == channel2, "the mod wheel works on any MIDI channel");
}

//==============================================================================
// Retrigger: every note restarting the LFO.

ProcessorSetUp withRetriggeredLfo (float rateHz, float depthPercent, int destination = pitch)
{
    return [=] (juce::AudioProcessor& p)
    {
        withLfo (rateHz, depthPercent, destination) (p);
        setParameter (p, "lfoRetrigger", 1.0f);
    };
}

void testRetriggerOffByDefault()
{
    const std::vector<TimedMessage> notes { { 0, noteOn (60) }, { 17000, noteOn (64) } };
    const auto byDefault = render ({ .events = notes, .setUp = withLfo (1.0f, 50.0f) });
    const auto switchedOff = render ({ .events = notes,
                                       .setUp = [] (juce::AudioProcessor& p)
                                       {
                                           withLfo (1.0f, 50.0f) (p);
                                           setParameter (p, "lfoRetrigger", 0.0f);
                                       } });
    check (byDefault == switchedOff, "LFO Retrigger is off by default: the LFO runs freely");
}

void testRetriggerStartsEachNoteAtTheBeginning()
{
    // A note started 0.37 s in, 1 Hz, 50 % on the pitch. With retrigger on, its vibrato starts from
    // the beginning of the LFO's cycle: at its top (523 Hz) a quarter of a second after the note, and
    // at its bottom (370 Hz) three quarters after. Running freely, it would be elsewhere.
    constexpr int noteStart = 17760;
    const std::vector<TimedMessage> note { { noteStart, noteOn (69) } };
    const auto output = render ({ .numSamples = 72000, .events = note, .setUp = withRetriggeredLfo (1.0f, 50.0f) });
    double worst = 0.0;

    for (auto secondsAfterNote : { 0.25, 0.75 })
    {
        const auto from = noteStart + (int) (secondsAfterNote * testSampleRate) - 480;
        const auto measured = frequencyBetween (output, from, from + 960, testSampleRate);
        const auto expected = expectedVibratoFrequency (1.0, 3.0, from - noteStart, from - noteStart + 960, sine);
        std::printf ("      %.2f s after the note: %.2f Hz (expected %.2f)\n", secondsAfterNote, measured, expected);
        worst = std::max (worst, std::abs (measured / expected - 1.0));
    }

    check (worst < 0.002, "with retrigger on, each note starts the LFO from the beginning of its cycle");
}

// A held A4 with full-depth tremolo, and the LFO at the top of its cycle (full level) at sample 12345,
// where very quiet notes start: one, or a chord of three. With retrigger on they restart the LFO,
// which takes the held note's level from full towards half. Returns the largest sample-to-sample step
// around the restart, against the largest in the 20 ms before it. (12345 doesn't land on one of A4's
// zero crossings, which would hide a jump.)
std::pair<float, float> stepsAroundRestart (int notesInChord)
{
    constexpr int restartSample = 12345;
    std::vector<TimedMessage> events { { 0, noteOn (69) } };

    for (int i = 0; i < notesInChord; ++i)
        events.push_back ({ restartSample, noteOn (72 + 4 * i, 1) });

    const auto output = render ({ .events = events,
                                  .setUp = [] (juce::AudioProcessor& p)
                                  {
                                      withRetriggeredLfo ((float) (0.25 * testSampleRate / restartSample), 100.0f, volume) (p);
                                      setParameter (p, "velocitySensitivity", 100.0f);
                                  } });
    const auto steady = largestStep (std::vector<float> (output.begin() + restartSample - 960, output.begin() + restartSample));
    const auto atRestart = largestStep (std::vector<float> (output.begin() + restartSample - 10, output.begin() + restartSample + 480));
    return { steady, atRestart };
}

void testRetriggerFadesNotesAlreadySounding()
{
    // The restart fades over 5 ms, so the held note's level glides down rather than dropping at once,
    // and a chord's notes, each restarting the LFO on the same sample, fade once rather than cutting
    // each other's fade short.
    const auto [steady, oneNote] = stepsAroundRestart (1);
    const auto [steadyChord, chord] = stepsAroundRestart (3);
    std::printf ("      largest step: %.4f before a restart, %.4f around it (one note), %.4f around it (a chord)\n", steady, oneNote, chord);
    check (oneNote <= steady * 1.1f && chord <= steadyChord * 1.1f,
           "a note restarting the LFO fades it, so notes already sounding don't jump, even for a chord");
}

void testRetriggerBlockSizeIndependence()
{
    const std::vector<TimedMessage> events { { 3, noteOn (60) }, { 1000, noteOn (64) }, { 5003, noteOn (67) }, { 5003, noteOn (71) } };
    const auto setUp = withRetriggeredLfo (6.0f, 60.0f, volume);
    auto renderInBlocksOf = [&] (int blockSize)
    {
        return render ({ .blockSize = blockSize, .numSamples = 8000, .events = events, .setUp = setUp });
    };
    const auto blocksOf1 = renderInBlocksOf (1);
    check (blocksOf1 == renderInBlocksOf (7) && blocksOf1 == renderInBlocksOf (512) && blocksOf1 == renderInBlocksOf (2048),
           "with retrigger on, output is identical with block sizes 1, 7, 512 and 2048");
}

//==============================================================================
// Tempo sync: the LFO's rate as a note length at the host's tempo, and its position locked to the
// song's while the host plays.

// LFO Sync Rate choices, spelled as the positions projects save, like the parameter tests.
constexpr int wholeNote = 2, dottedQuarter = 6, quarter = 7, eighth = 10, eighthTriplet = 11, thirtySecond = 15;

ProcessorSetUp withSyncedLfo (int syncRate, float depthPercent, int destination = pitch)
{
    return [=] (juce::AudioProcessor& p)
    {
        withLfo (1.0f, depthPercent, destination) (p);
        setParameter (p, "lfoSync", 1.0f);
        setParameter (p, "lfoSyncRate", (float) syncRate);
    };
}

// A host playing at `bpm`, its song `startQuarterNotes` in when rendering starts.
std::function<juce::AudioPlayHead::PositionInfo (int)> hostPlaying (double bpm, double startQuarterNotes)
{
    return [=] (int blockStart)
    {
        juce::AudioPlayHead::PositionInfo position;
        position.setBpm (bpm);
        position.setPpqPosition (startQuarterNotes + blockStart / testSampleRate * bpm / 60.0);
        position.setIsPlaying (true);
        return position;
    };
}

// A host stopped, with its tempo set to `bpm`.
std::function<juce::AudioPlayHead::PositionInfo (int)> hostStopped (double bpm)
{
    return [=] (int)
    {
        juce::AudioPlayHead::PositionInfo position;
        position.setBpm (bpm);
        position.setPpqPosition (0.0);
        position.setIsPlaying (false);
        return position;
    };
}

void testLfoSyncOffByDefault()
{
    // With sync off (the default), the host's tempo and song position make no difference.
    const auto withoutHost = render ({ .events = heldNote, .setUp = withLfo (1.0f, 50.0f) });
    const auto withHost = render ({ .events = heldNote, .setUp = withLfo (1.0f, 50.0f), .transport = hostPlaying (97.0, 3.3) });
    check (withoutHost == withHost, "LFO Sync is off by default: the host's tempo makes no difference");
}

void testSyncedRateFollowsTheTempo()
{
    // A synced LFO runs at its note length at the tempo. With the host stopped (or no host) it runs
    // freely, like the Rate knob's. Its speed should depend only on the length and the tempo:
    //   - 1/4 at 120 BPM (the tempo used when the host gives none) and 1/8 at 60 BPM are both 2 Hz;
    //   - a dotted 1/4 at 90 BPM is 1 Hz, the same as the Rate knob at 1 Hz (its centre, the one
    //     setting it holds exactly: its skewed range rounds others, such as 2 Hz, by a hair);
    //   - a 1/8 triplet at 150 BPM is 7.5 Hz, measured from the vibrato it gives.
    const auto quarterAt120 = render ({ .events = heldNote, .setUp = withSyncedLfo (quarter, 50.0f) });
    const auto eighthAt60 = render ({ .events = heldNote, .setUp = withSyncedLfo (eighth, 50.0f), .transport = hostStopped (60.0) });
    const auto dottedQuarterAt90 = render ({ .events = heldNote, .setUp = withSyncedLfo (dottedQuarter, 50.0f), .transport = hostStopped (90.0) });
    const auto oneHz = render ({ .events = heldNote, .setUp = withLfo (1.0f, 50.0f) });
    const auto eighthTripletAt150 = render ({ .events = heldNote, .setUp = withSyncedLfo (eighthTriplet, 50.0f), .transport = hostStopped (150.0) });
    check (quarterAt120 == eighthAt60, "a synced LFO's speed depends on its note length and the tempo (or 120 BPM without one)");
    check (dottedQuarterAt90 == oneHz, "a synced LFO at 1 Hz runs exactly as the Rate knob at 1 Hz does");
    std::printf ("      1/8 triplet at 150 BPM:\n");
    check (worstVibratoError (eighthTripletAt150, 7.5, 3.0, { 0.5, 0.567, 0.633 }) < 0.003, "a 1/8 triplet at 150 BPM runs at 7.5 Hz");
}

void testSyncLocksToTheSong()
{
    // The host plays at 120 BPM from 3.3 quarter notes into the song; synced to 1/1, the LFO's cycle
    // lasts 4 quarter notes (2 s). Its position should follow the song's: a cycle counted from the
    // song's start, so here it begins 3.3 / 4 of the way through. (It's at 0 when the block starts,
    // so it fades there over the first 5 ms.)
    constexpr double startQuarterNotes = 3.3, bpm = 120.0;
    const auto output = render ({ .events = heldNote, .setUp = withSyncedLfo (wholeNote, 50.0f), .transport = hostPlaying (bpm, startQuarterNotes) });
    double worst = 0.0;

    for (auto centre : { 0.2, 0.45, 0.8 })
    {
        const auto from = (int) (centre * testSampleRate) - 480;
        double sum = 0.0;

        for (int i = from; i < from + 960; ++i)
        {
            const auto cycles = (startQuarterNotes + i / testSampleRate * bpm / 60.0) / 4.0;
            sum += 440.0 * std::exp2 (3.0 * std::sin (juce::MathConstants<double>::twoPi * cycles) / 12.0);
        }

        const auto measured = frequencyBetween (output, from, from + 960, testSampleRate);
        std::printf ("      at %.2f s: %.2f Hz (expected %.2f)\n", centre, measured, sum / 960.0);
        worst = std::max (worst, std::abs (measured / (sum / 960.0) - 1.0));
    }

    check (worst < 0.002, "while the host plays, a synced LFO's position follows the song's");
}

void testSyncedBlockSizeIndependence()
{
    // Locked to the song, the LFO is never moved in ordinary playback, so the host's block size makes
    // no difference.
    const std::vector<TimedMessage> events { { 3, noteOn (60) }, { 1000, noteOn (64) } };
    auto renderInBlocksOf = [&] (int blockSize)
    {
        return render ({ .blockSize = blockSize, .numSamples = 8000, .events = events,
                         .setUp = withSyncedLfo (eighthTriplet, 60.0f, volume), .transport = hostPlaying (133.0, 7.77) });
    };
    const auto blocksOf1 = renderInBlocksOf (1);
    check (blocksOf1 == renderInBlocksOf (7) && blocksOf1 == renderInBlocksOf (512) && blocksOf1 == renderInBlocksOf (2048),
           "locked to the song, output is identical with block sizes 1, 7, 512 and 2048");
}

void testSongJumpFades()
{
    // Full-depth tremolo synced to 1/1 at 120 BPM. At a block start the host jumps back half a cycle
    // in the song (as when it loops), taking the LFO from near its top (full level) to near its bottom
    // (silence). It fades there over 5 ms rather than dropping at once; after that it's exactly where
    // a song that had always been there would put it.
    constexpr double bpm = 120.0, quartersPerSample = bpm / 60.0 / testSampleRate;
    const auto changeQuarters = changeSample * quartersPerSample;
    const auto beforeJump = 1.0 - changeQuarters; // the song is at 1 quarter note (just after the LFO's top) at the jump
    const auto afterJump = 3.0 - changeQuarters;  // and then at 3 (just after its bottom)
    auto transportWithJump = [=] (int blockStart)
    {
        return hostPlaying (bpm, blockStart < changeSample ? beforeJump : afterJump) (blockStart);
    };
    const auto setUp = withSyncedLfo (wholeNote, 100.0f, volume);
    const auto jumped = render ({ .events = heldNote, .setUp = setUp, .transport = transportWithJump });
    const auto alwaysThere = render ({ .events = heldNote, .setUp = setUp, .transport = hostPlaying (bpm, afterJump) });
    const auto steady = largestStep (std::vector<float> (jumped.begin() + changeSample - 960, jumped.begin() + changeSample));
    const auto atJump = largestStep (std::vector<float> (jumped.begin() + changeSample - 10, jumped.begin() + changeSample + 480));
    float differenceAfterFade = 0.0f;

    for (int i = changeSample + 240; i < 48000; ++i)
        differenceAfterFade = std::max (differenceAfterFade, std::abs (jumped[(size_t) i] - alwaysThere[(size_t) i]));

    std::printf ("      song jump: largest step %.4f before it, %.4f around it; afterwards %.1e from a song that was always there\n",
                 steady, atJump, differenceAfterFade);
    check (atJump <= steady * 1.1f, "when the song jumps, a synced LFO fades to its new position rather than jumping");
    check (differenceAfterFade < 1.0e-5f, "after the fade, the LFO is where the song's new position puts it");
}

void testRetriggerWinsOverSync()
{
    // Synced to 1/1 at 120 BPM (a 2 s cycle) with the host playing, but with retrigger on, a note at
    // 0.37 s starts the LFO from the beginning of its cycle: at its top half a second after the note.
    // The song starts 1.3 quarter notes in, so it would put the LFO half a cycle away when the note
    // starts (2.04 quarter notes in): at its bottom then, not its top.
    constexpr int noteStart = 17760;
    const auto output = render ({ .numSamples = 72000,
                                  .events = { { noteStart, noteOn (69) } },
                                  .setUp = [] (juce::AudioProcessor& p)
                                  {
                                      withSyncedLfo (wholeNote, 50.0f) (p);
                                      setParameter (p, "lfoRetrigger", 1.0f);
                                  },
                                  .transport = hostPlaying (120.0, 1.3) });
    const auto from = noteStart + 24000 - 480;
    const auto measured = frequencyBetween (output, from, from + 960, testSampleRate);
    const auto expected = expectedVibratoFrequency (0.5, 3.0, from - noteStart, from - noteStart + 960, sine);
    std::printf ("      half a second after the note: %.2f Hz (expected %.2f)\n", measured, expected);
    check (std::abs (measured / expected - 1.0) < 0.002, "with retrigger on, notes restart a synced LFO rather than the song placing it");
}

void testVeryFastSyncIsCapped()
{
    // 1/32 at 375 BPM is 50 Hz, the fastest a synced LFO runs; at 600 BPM it would be 80 Hz, but it
    // stays at 50.
    const auto setUp = withSyncedLfo (thirtySecond, 100.0f, volume);
    const auto at375 = render ({ .events = heldNote, .setUp = setUp, .transport = hostStopped (375.0) });
    const auto at600 = render ({ .events = heldNote, .setUp = setUp, .transport = hostStopped (600.0) });
    check (at375 == at600, "a synced LFO runs no faster than 50 Hz, whatever the tempo");
}

//==============================================================================
// The Lfo class on its own.

// The jump width, in radians, the synth gives the LFO at a rate: 5 ms of its cycle.
double jumpWidthAt (double rateHz)
{
    return juce::MathConstants<double>::twoPi * rateHz * boringsynth::Synth::lfoJumpSeconds;
}

void testLfoShapesAreExactAwayFromJumps()
{
    // Sampled finely over the cycle at 1 Hz: the sine and triangle are exactly the waveforms' shapes
    // everywhere; the square is exactly 1 or -1 away from its jumps; the saw is a straight line from -1
    // to 1 between them. And every shape is exactly 0 at phase 0, where the LFO starts.
    constexpr auto pi = juce::MathConstants<double>::pi;
    const auto width = jumpWidthAt (1.0);
    double sineAndTriangleError = 0.0, squareError = 0.0, sawError = 0.0;

    for (int i = 0; i < 100000; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * i / 100000.0;
        const auto x = phase < pi ? phase : phase - 2.0 * pi;
        const auto awayFromJumps = std::abs (x) > width && pi - std::abs (x) > width;

        for (auto shape : { sine, triangle })
            sineAndTriangleError = std::max (sineAndTriangleError,
                                             (double) std::abs (boringsynth::Lfo::getValue (shape, phase, width) - boringsynth::waveformShape (shape, phase)));

        if (awayFromJumps)
            squareError = std::max (squareError, std::abs (boringsynth::Lfo::getValue (square, phase, width) - (x < 0.0 ? -1.0 : 1.0)));

        if (pi - std::abs (x) > width)
            sawError = std::max (sawError, std::abs (boringsynth::Lfo::getValue (saw, phase, width) - x / (pi - 0.5 * width)));
    }

    bool allZeroAtStart = true;

    for (auto shape : { sine, triangle, saw, square })
        allZeroAtStart = allZeroAtStart && boringsynth::Lfo::getValue (shape, 0.0, width) == 0.0f;

    std::printf ("      largest errors: sine and triangle %.1e, square's flat tops %.1e, saw's ramp %.1e\n",
                 sineAndTriangleError, squareError, sawError);
    check (sineAndTriangleError < 1.0e-6, "the LFO's sine and triangle are exactly the waveforms' shapes");
    check (squareError == 0.0, "the LFO's square is exactly 1 or -1 away from its jumps");
    check (sawError < 1.0e-6, "the LFO's saw rises in a straight line between its jumps");
    check (allZeroAtStart, "every LFO shape is exactly 0 at phase 0");
}

void testLfoShapesReachExactlyOne()
{
    // At every rate, every shape swings exactly to -1 and 1, and never past them.
    bool allExact = true;

    for (auto rateHz : { 0.05, 1.0, 5.0, 20.0 })
        for (auto shape : { sine, triangle, saw, square })
        {
            float lowest = 0.0f, highest = 0.0f;

            for (int i = 0; i <= 200000; ++i)
            {
                const auto value = boringsynth::Lfo::getValue (shape, juce::MathConstants<double>::twoPi * i / 200001.0, jumpWidthAt (rateHz));
                lowest = std::min (lowest, value);
                highest = std::max (highest, value);
            }

            allExact = allExact && lowest >= -1.0f && highest <= 1.0f && lowest < -0.99999f && highest > 0.99999f;
        }

    check (allExact, "at every rate every LFO shape swings exactly to -1 and 1, and never past them");
}

void testLfoJumpsTakeFiveMs()
{
    // Run as the synth runs it, the saw's and square's jumps take 5 ms at every rate: the steepest
    // step is 2 / 240 a sample. The sine and triangle move far more gently.
    const auto jumpSamples = boringsynth::Synth::lfoJumpSeconds * testSampleRate;
    double worstJumpError = 0.0;
    float steepestSmooth = 0.0f;

    for (auto rateHz : { 0.05, 1.0, 5.0, 20.0 })
    {
        for (auto shape : { sine, triangle, saw, square })
        {
            boringsynth::Lfo lfo;
            lfo.setShape (shape, 0);
            const auto phaseDelta = juce::MathConstants<double>::twoPi * rateHz / testSampleRate;
            auto previous = lfo.getNextSample (phaseDelta, jumpSamples);
            float steepest = 0.0f;

            for (int i = 1; i < (int) (2.0 * testSampleRate / rateHz); ++i)
            {
                const auto value = lfo.getNextSample (phaseDelta, jumpSamples);
                steepest = std::max (steepest, std::abs (value - previous));
                previous = value;
            }

            const auto jumpMs = 1000.0 * 2.0 / steepest / testSampleRate;

            if (shape == saw || shape == square)
            {
                std::printf ("      %5.2f Hz %-6s: steepest edge crosses -1 to 1 in %.2f ms\n", rateHz, shape == saw ? "saw" : "square", jumpMs);
                worstJumpError = std::max (worstJumpError, std::abs (jumpMs - 5.0));
            }
            else
            {
                steepestSmooth = std::max (steepestSmooth, steepest);
            }
        }
    }

    check (worstJumpError < 0.05, "at every rate the saw's and square's jumps take 5 ms, so they don't click");
    check (steepestSmooth < 2.0f / 240.0f, "the sine and triangle never move as fast as a jump does");
}

void testLfoShapeCrossfade()
{
    // Switching shape blends from the old to the new over the crossfade, then is exactly the new
    // shape, at the same phase.
    constexpr int fadeLength = 480;
    const auto phaseDelta = juce::MathConstants<double>::twoPi * 3.0 / testSampleRate;
    boringsynth::Lfo fading, sineOnly, squareOnly;
    squareOnly.setShape (square, 0);
    float largestBlendError = 0.0f;
    bool exactAfterFade = true;

    for (int i = 0; i < 4000; ++i)
    {
        if (i == 1000)
            fading.setShape (square, fadeLength);

        const auto out = fading.getNextSample (phaseDelta, 240.0);
        const auto from = sineOnly.getNextSample (phaseDelta, 240.0);
        const auto to = squareOnly.getNextSample (phaseDelta, 240.0);

        if (i >= 1000 && i < 1000 + fadeLength - 1)
        {
            // The first sample after the switch is already 1 / fadeLength of the way there.
            const auto progress = (float) (i - 1000 + 1) / (float) fadeLength;
            largestBlendError = std::max (largestBlendError, std::abs (out - (from + progress * (to - from))));
        }
        else if (i >= 1000 + fadeLength)
        {
            exactAfterFade = exactAfterFade && out == to;
        }
    }

    check (largestBlendError < 1.0e-6f, "switching the LFO's shape blends the two over the crossfade");
    check (exactAfterFade, "after the crossfade the LFO is exactly the new shape");

    // Now a quarter of a cycle in, where the square is at 1, a reset takes it back to the start.
    fading.reset();
    check (fading.getNextSample (phaseDelta, 240.0) == 0.0f, "after a reset the LFO starts again from phase 0");
}

void testLfoRestart()
{
    // A restart blends from the wave carrying on where it was to the wave starting again from phase 0,
    // over the crossfade; then it's exactly the restarted wave. Restarting with no crossfade starts
    // again at once.
    constexpr int fadeLength = 240, restartAt = 1234;
    const auto phaseDelta = juce::MathConstants<double>::twoPi * 3.0 / testSampleRate;
    boringsynth::Lfo restarted, carriedOn, startedFresh;
    float largestBlendError = 0.0f;
    bool exactAfterFade = true;

    for (int i = 0; i < 4000; ++i)
    {
        if (i == restartAt)
            restarted.restart (fadeLength);

        const auto out = restarted.getNextSample (phaseDelta, 240.0);
        const auto from = carriedOn.getNextSample (phaseDelta, 240.0);
        const auto to = i >= restartAt ? startedFresh.getNextSample (phaseDelta, 240.0) : 0.0f;

        if (i >= restartAt && i < restartAt + fadeLength - 1)
        {
            const auto progress = (float) (i - restartAt + 1) / (float) fadeLength;
            largestBlendError = std::max (largestBlendError, std::abs (out - (from + progress * (to - from))));
        }
        else if (i >= restartAt + fadeLength)
        {
            exactAfterFade = exactAfterFade && out == to;
        }
    }

    check (largestBlendError < 1.0e-6f, "restarting the LFO blends from where it was to the restarted wave");
    check (exactAfterFade, "after the crossfade the LFO is exactly the restarted wave");

    restarted.restart (0);
    check (restarted.getNextSample (phaseDelta, 240.0) == 0.0f, "restarting with no crossfade starts again at once");
}

void testLfoRestartDuringAFade()
{
    // A 3 Hz sine restarted at its top (1) fades towards the restarted wave (near 0). Restarted again
    // halfway through that fade, from about 0.5, it carries on from there rather than jumping back
    // towards the first fade's outgoing wave (still near 1). And a shape change during a fade does
    // the same. Steps stay at most a little over a fade's 1 / 240 a sample, against 0.5 for a jump.
    constexpr int fadeLength = 240, firstRestart = 4000, secondRestart = firstRestart + 120;
    const auto phaseDelta = juce::MathConstants<double>::twoPi * 3.0 / testSampleRate;
    boringsynth::Lfo restartedTwice, restartedThenReshaped;

    // Reaches the top of the sine (a quarter cycle) at the first restart.
    const auto quarterCycle = (int) (0.25 * testSampleRate / 3.0);
    float previousRestarted = 0.0f, previousReshaped = 0.0f, largestStepRestarted = 0.0f, largestStepReshaped = 0.0f;

    for (int i = firstRestart - quarterCycle; i < secondRestart + 2 * fadeLength; ++i)
    {
        if (i == firstRestart)
        {
            restartedTwice.restart (fadeLength);
            restartedThenReshaped.restart (fadeLength);
        }

        if (i == secondRestart)
        {
            restartedTwice.restart (fadeLength);
            restartedThenReshaped.setShape (square, fadeLength);
        }

        const auto restarted = restartedTwice.getNextSample (phaseDelta, 240.0);
        const auto reshaped = restartedThenReshaped.getNextSample (phaseDelta, 240.0);

        if (i > firstRestart - quarterCycle)
        {
            largestStepRestarted = std::max (largestStepRestarted, std::abs (restarted - previousRestarted));
            largestStepReshaped = std::max (largestStepReshaped, std::abs (reshaped - previousReshaped));
        }

        previousRestarted = restarted;
        previousReshaped = reshaped;
    }

    std::printf ("      largest step through a fade interrupted by a restart %.4f, by a shape change %.4f (a fade's own: %.4f)\n",
                 largestStepRestarted, largestStepReshaped, 1.0f / fadeLength);
    check (largestStepRestarted < 2.0f / fadeLength && largestStepReshaped < 2.0f / fadeLength,
           "a restart or shape change part-way through an LFO fade carries on from where it had got to");
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
    testDefaultShapeIsSine();
    testLfoShapesOnPitch();
    testShapeChangeCrossfades();
    testSquareTremoloDoesntClick();
    testModWheelIgnoredByDefault();
    testModWheelAddsDepth();
    testModWheelDepthStopsAtFull();
    testModWheelGlides();
    testResetAllControllersDropsTheWheel();
    testModWheelOnAnyChannel();
    testRetriggerOffByDefault();
    testRetriggerStartsEachNoteAtTheBeginning();
    testRetriggerFadesNotesAlreadySounding();
    testRetriggerBlockSizeIndependence();
    testLfoSyncOffByDefault();
    testSyncedRateFollowsTheTempo();
    testSyncLocksToTheSong();
    testSyncedBlockSizeIndependence();
    testSongJumpFades();
    testRetriggerWinsOverSync();
    testVeryFastSyncIsCapped();
    testLfoShapesAreExactAwayFromJumps();
    testLfoShapesReachExactlyOne();
    testLfoJumpsTakeFiveMs();
    testLfoShapeCrossfade();
    testLfoRestart();
    testLfoRestartDuringAFade();
}

} // namespace testing
