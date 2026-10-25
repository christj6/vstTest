#include "TestHelpers.h"

// Note tests: levels, the Nyquist limit, timing, pedals, block sizes and voice stealing, rendered through the whole plug-in.

namespace testing
{

namespace
{

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

void testStolenNoteKeepsLfoPitch()
{
    // The LFO, at full depth, is at the top of its cycle (an octave up) when the note is stolen. As it
    // fades, the stolen note should stay at that pitch, matching the unstolen note faded the same
    // way, rather than dropping back an octave.
    const auto withLfo = [] (juce::AudioProcessor& p)
    {
        quieter (p);
        setParameter (p, "lfoRate", (float) (0.25 * testSampleRate / stealSample));
        setParameter (p, "lfoDepth", 100.0f);
    };
    auto events = sixteenMiddleCs();
    events.push_back ({ stealSample, noteOn (64) });
    const auto withSteal = render ({ .numSamples = 9600, .events = events, .setUp = withLfo });
    auto eventsWithoutStolenNote = sixteenMiddleCs (2);
    eventsWithoutStolenNote.push_back ({ stealSample, noteOn (64) });
    const auto withoutStolenNote = render ({ .numSamples = 9600, .events = eventsWithoutStolenNote, .setUp = withLfo });
    const auto unstolen = render ({ .numSamples = 9600, .events = { { 0, noteOn (60) } }, .setUp = withLfo });

    const auto peak = 0.1f * juce::Decibels::decibelsToGain (-12.0f);
    float largestDifference = 0.0f;

    for (int i = 0; i < 120; ++i)
    {
        const auto index = (size_t) (stealSample + i);
        const auto stolenNote = withSteal[index] - withoutStolenNote[index];
        const auto fade = 1.0f - (float) i / 240.0f;
        largestDifference = std::max (largestDifference, std::abs (stolenNote - fade * unstolen[index]));
    }

    std::printf ("      first half of the fade: the stolen note differs from the unstolen one by at most %.4f (its peak is %.4f)\n",
                 largestDifference, peak);
    check (largestDifference < 0.1f * peak, "a stolen note keeps the LFO's pitch as it fades");
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

void testStolenNoteKeepsItsFilter()
{
    // The same steal, with a resonant band-pass filter on. As it starts to fade, the stolen note
    // should still be filtered the same way: as the note would have been if never stolen, not
    // suddenly brighter or switched to another filter type.
    const ProcessorSetUp quieterAndFiltered = [] (juce::AudioProcessor& p)
    {
        setParameter (p, "volume", -12.0f);
        setParameter (p, "filterEnabled", 1.0f);
        setParameter (p, "filterCutoff", 300.0f);
        setParameter (p, "filterResonance", 50.0f);
        setParameter (p, "filterType", 2.0f);
    };

    auto withSteal = sixteenMiddleCs();
    withSteal.push_back ({ stealSample, noteOn (64) });
    auto withoutStolenNote = sixteenMiddleCs (2);
    withoutStolenNote.push_back ({ stealSample, noteOn (64) });

    const auto stolen = render ({ .numSamples = 9600, .events = withSteal, .setUp = quieterAndFiltered });
    const auto notStolen = render ({ .numSamples = 9600, .events = withoutStolenNote, .setUp = quieterAndFiltered });
    const auto alone = render ({ .numSamples = 9600, .events = { { 0, noteOn (60) } }, .setUp = quieterAndFiltered });

    const auto stolenNote = stolen[(size_t) stealSample] - notStolen[(size_t) stealSample];
    std::printf ("      filtered stolen note when stolen: %.5f (the note alone, filtered: %.5f)\n", stolenNote, alone[(size_t) stealSample]);
    check (std::abs (stolenNote - alone[(size_t) stealSample]) < 1.0e-5f, "a stolen note keeps its filter as it fades");
}

} // namespace

void runNoteTests()
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
    testVoiceStealDoesNotClick();
    testStolenNoteFadesOut();
    testStolenNoteKeepsLfoPitch();
    testAllSoundOffFades();
    testStolenNoteKeepsItsFilter();
}

} // namespace testing
