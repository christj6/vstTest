#include "TestHelpers.h"

// Parameter tests: the parameters exist with the IDs saved projects rely on, and the plug-in's
// state survives a save and reload.

namespace testing
{

namespace
{

std::unique_ptr<juce::AudioProcessor> createProcessor()
{
    return std::unique_ptr<juce::AudioProcessor> (createPluginFilter());
}

void testVolumeParameter()
{
    auto processor = createProcessor();

    // The ID is spelled out here on purpose, not shared with the plug-in's code: if someone
    // renames it there, this fails, because saved projects would lose their volume setting.
    check (findParameter (*processor, "volume") != nullptr, "parameter ID 'volume' exists");
    check (std::abs (getParameter (*processor, "volume")) < 0.01f, "volume defaults to 0 dB");
}

void testVolumeText()
{
    auto processor = createProcessor();
    auto* volume = findParameter (*processor, "volume");

    if (volume == nullptr)
        return;

    check (volume->getText (volume->convertTo0to1 (-6.0f), 10) == "-6.0", "volume -6 dB displays as '-6.0'");
    check (volume->getText (0.0f, 10) == "-inf", "minimum volume displays as '-inf'");
    check (std::abs (volume->convertFrom0to1 (volume->getValueForText ("-inf")) - (-60.0f)) < 0.01f,
           "typing '-inf' sets the minimum volume");
}

void testVelocitySensitivityParameter()
{
    auto processor = createProcessor();
    auto* sensitivity = findParameter (*processor, "velocitySensitivity");

    // Spelled out on purpose, like 'volume' above.
    check (sensitivity != nullptr, "parameter ID 'velocitySensitivity' exists");

    if (sensitivity == nullptr)
        return;

    check (std::abs (getParameter (*processor, "velocitySensitivity")) < 0.01f, "velocity sensitivity defaults to 0 %");
    check (sensitivity->getText (sensitivity->convertTo0to1 (50.0f), 10) == "50", "50 % sensitivity displays as '50'");
}

void testPitchBendRangeParameter()
{
    auto processor = createProcessor();
    auto* range = findParameter (*processor, "pitchBendRange");

    // Spelled out on purpose, like 'volume' above.
    check (range != nullptr, "parameter ID 'pitchBendRange' exists");

    if (range == nullptr)
        return;

    check (std::abs (getParameter (*processor, "pitchBendRange") - 2.0f) < 0.01f, "pitch bend range defaults to 2 semitones");
    check (range->getText (range->convertTo0to1 (12.0f), 10) == "12", "a 12 semitone range displays as '12'");
    check (std::abs (range->convertFrom0to1 (1.0f) - 24.0f) < 0.01f, "pitch bend range goes up to 24 semitones");
}

void testWaveformParameter()
{
    auto processor = createProcessor();
    auto* waveform = findParameter (*processor, "waveform");

    // Spelled out on purpose, like 'volume' above.
    check (waveform != nullptr, "parameter ID 'waveform' exists");

    if (waveform == nullptr)
        return;

    check (std::abs (getParameter (*processor, "waveform")) < 0.01f, "the waveform defaults to the first choice");

    // A DAW saves the choice by position, so the list must keep this order forever. New waveforms
    // can only be added to the end.
    const juce::StringArray expected { "Sine", "Triangle", "Saw", "Square" };
    bool inOrder = true;

    for (int i = 0; i < expected.size(); ++i)
        inOrder = inOrder && waveform->getText (waveform->convertTo0to1 ((float) i), 20) == expected[i];

    check (inOrder, "the waveform choices are Sine, Triangle, Saw and Square, in that order");
}

void testAmpEnvelopeParameters()
{
    auto processor = createProcessor();

    // Spelled out on purpose, like 'volume' above. "amp" leaves room for a filter envelope.
    bool allExist = true;

    for (auto* id : { "ampAttack", "ampDecay", "ampSustain", "ampRelease" })
        allExist = allExist && findParameter (*processor, id) != nullptr;

    check (allExist, "parameter IDs 'ampAttack', 'ampDecay', 'ampSustain' and 'ampRelease' exist");

    if (! allExist)
        return;

    // The defaults reproduce the plug-in before the envelope: instant, held, 5 ms release.
    check (getParameter (*processor, "ampAttack") == 0.0f
               && std::abs (getParameter (*processor, "ampSustain") - 100.0f) < 0.01f
               && std::abs (getParameter (*processor, "ampRelease") - 0.005f) < 1.0e-6f,
           "the amp envelope defaults to an instant attack, full sustain and a 5 ms release");

    auto* attack = findParameter (*processor, "ampAttack");
    auto* decay = findParameter (*processor, "ampDecay");
    auto* release = findParameter (*processor, "ampRelease");

    check (release->getText (release->getValue(), 20) == "5 ms" && attack->getText (attack->convertTo0to1 (1.5f), 20) == "1.50 s",
           "times show in milliseconds under a second ('5 ms') and seconds above ('1.50 s')");
    check (std::abs (attack->convertFrom0to1 (attack->getValueForText ("250 ms")) - 0.25f) < 0.001f
               && std::abs (attack->convertFrom0to1 (attack->getValueForText ("2 s")) - 2.0f) < 0.001f,
           "typing '250 ms' or '2 s' sets those times");
    check (std::abs (attack->convertFrom0to1 (0.5f) - 0.5f) < 0.01f && std::abs (release->convertFrom0to1 (0.5f) - 0.5f) < 0.01f,
           "the middle of a time knob is half a second, leaving most of its travel to short times");
    check (std::abs (decay->convertFrom0to1 (0.0f) - 0.005f) < 1.0e-6f && std::abs (release->convertFrom0to1 (0.0f) - 0.005f) < 1.0e-6f
               && std::abs (release->convertFrom0to1 (1.0f) - 10.0f) < 0.001f,
           "decay and release go from 5 ms (so they never click) up to 10 s");

    // The DAW is told how long notes keep sounding after their keys are released.
    setParameter (*processor, "ampRelease", 2.0f);
    check (std::abs (processor->getTailLengthSeconds() - 2.0) < 0.001, "the plug-in reports its release time as its tail length");
}

void testFilterParameters()
{
    auto processor = createProcessor();

    // Spelled out on purpose, like 'volume' above.
    bool allExist = true;

    for (auto* id : { "filterEnabled", "filterCutoff", "filterResonance" })
        allExist = allExist && findParameter (*processor, id) != nullptr;

    check (allExist, "parameter IDs 'filterEnabled', 'filterCutoff' and 'filterResonance' exist");

    if (! allExist)
        return;

    check (getParameter (*processor, "filterEnabled") == 0.0f
               && std::abs (getParameter (*processor, "filterCutoff") - 20000.0f) < 0.5f
               && getParameter (*processor, "filterResonance") == 0.0f,
           "the filter defaults to off, with the cutoff fully open and no resonance");

    auto* cutoff = findParameter (*processor, "filterCutoff");
    check (cutoff->getText (cutoff->convertTo0to1 (440.0f), 20) == "440 Hz" && cutoff->getText (cutoff->convertTo0to1 (1200.0f), 20) == "1.20 kHz",
           "the cutoff shows in Hz under 1 kHz ('440 Hz') and kHz above ('1.20 kHz')");
    check (std::abs (cutoff->convertFrom0to1 (cutoff->getValueForText ("1.2 kHz")) - 1200.0f) < 0.5f
               && std::abs (cutoff->convertFrom0to1 (cutoff->getValueForText ("3k")) - 3000.0f) < 0.5f
               && std::abs (cutoff->convertFrom0to1 (cutoff->getValueForText ("440 Hz")) - 440.0f) < 0.5f,
           "typing '1.2 kHz', '3k' or '440 Hz' sets those cutoffs");
    check (std::abs (cutoff->convertFrom0to1 (0.5f) - 632.46f) < 1.0f && std::abs (cutoff->convertFrom0to1 (0.0f) - 20.0f) < 0.01f,
           "the cutoff knob runs from 20 Hz, with its middle at about 632 Hz (even steps in pitch)");

    // The filter type, spelled out on purpose like the IDs above. A DAW saves the choice by
    // position, so the list must keep this order forever; new types can only go at the end.
    auto* type = findParameter (*processor, "filterType");
    check (type != nullptr, "parameter ID 'filterType' exists");

    if (type == nullptr)
        return;

    const juce::StringArray expected { "Low-pass", "High-pass", "Band-pass" };
    bool inOrder = true;

    for (int i = 0; i < expected.size(); ++i)
        inOrder = inOrder && type->getText (type->convertTo0to1 ((float) i), 20) == expected[i];

    check (getParameter (*processor, "filterType") == 0.0f, "the filter type defaults to the first choice (low-pass)");
    check (inOrder, "the filter types are Low-pass, High-pass and Band-pass, in that order");

    // Key tracking, spelled out on purpose like the IDs above.
    auto* keyTracking = findParameter (*processor, "filterKeyTracking");
    check (keyTracking != nullptr, "parameter ID 'filterKeyTracking' exists");

    if (keyTracking == nullptr)
        return;

    check (getParameter (*processor, "filterKeyTracking") == 0.0f
               && std::abs (keyTracking->convertFrom0to1 (1.0f) - 100.0f) < 0.01f
               && keyTracking->getText (keyTracking->convertTo0to1 (50.0f), 20) == "50",
           "key tracking runs from 0 to 100 %, defaults to 0 % and displays as a number ('50')");
}

void testFilterEnvelopeParameters()
{
    auto processor = createProcessor();

    // Spelled out on purpose, like 'volume' above.
    bool allExist = true;

    for (auto* id : { "filterAttack", "filterDecay", "filterSustain", "filterRelease", "filterEnvAmount" })
        allExist = allExist && findParameter (*processor, id) != nullptr;

    check (allExist, "parameter IDs 'filterAttack', 'filterDecay', 'filterSustain', 'filterRelease' and 'filterEnvAmount' exist");

    if (! allExist)
        return;

    // The amount defaults to no effect; the shape to a pluck, ready for when the amount is turned up.
    check (getParameter (*processor, "filterEnvAmount") == 0.0f
               && getParameter (*processor, "filterAttack") == 0.0f
               && std::abs (getParameter (*processor, "filterDecay") - 0.3f) < 1.0e-5f
               && getParameter (*processor, "filterSustain") == 0.0f
               && std::abs (getParameter (*processor, "filterRelease") - 0.3f) < 1.0e-5f,
           "the filter envelope defaults to no effect (amount 0), shaped as a 300 ms pluck");

    auto* amount = findParameter (*processor, "filterEnvAmount");
    check (amount->getText (amount->convertTo0to1 (2.0f), 20) == "+2.00 oct"
               && amount->getText (amount->convertTo0to1 (-1.5f), 20) == "-1.50 oct"
               && amount->getText (amount->convertTo0to1 (0.0f), 20) == "0.00 oct",
           "the envelope amount displays in octaves ('+2.00 oct', '-1.50 oct', '0.00 oct')");
    check (std::abs (amount->convertFrom0to1 (0.0f) + 8.0f) < 0.001f && std::abs (amount->convertFrom0to1 (1.0f) - 8.0f) < 0.001f,
           "the envelope amount runs from -8 to +8 octaves");
}

void testFilterVelocityParameter()
{
    auto processor = createProcessor();

    // Spelled out on purpose, like 'volume' above.
    auto* velocity = findParameter (*processor, "filterVelocity");
    check (velocity != nullptr, "parameter ID 'filterVelocity' exists");

    if (velocity == nullptr)
        return;

    check (getParameter (*processor, "filterVelocity") == 0.0f, "Filter Velocity defaults to 0 (velocity ignored)");
    check (velocity->convertFrom0to1 (0.0f) == 0.0f && std::abs (velocity->convertFrom0to1 (1.0f) - 8.0f) < 0.001f,
           "Filter Velocity runs from 0 to 8 octaves");
    check (velocity->getText (velocity->convertTo0to1 (2.5f), 20) == "2.50 oct"
               && velocity->getText (velocity->convertTo0to1 (0.0f), 20) == "0.00 oct",
           "Filter Velocity displays in octaves ('2.50 oct', '0.00 oct')");
}

void testLfoParameters()
{
    auto processor = createProcessor();

    // Spelled out on purpose, like 'volume' above.
    auto* rate = findParameter (*processor, "lfoRate");
    auto* depth = findParameter (*processor, "lfoDepth");
    check (rate != nullptr && depth != nullptr, "parameter IDs 'lfoRate' and 'lfoDepth' exist");

    if (rate == nullptr || depth == nullptr)
        return;

    check (std::abs (getParameter (*processor, "lfoRate") - 5.0f) < 1.0e-4f && getParameter (*processor, "lfoDepth") == 0.0f,
           "the LFO defaults to 5 Hz with no depth (no effect)");
    check (std::abs (rate->convertFrom0to1 (0.0f) - 0.05f) < 1.0e-5f && std::abs (rate->convertFrom0to1 (1.0f) - 20.0f) < 1.0e-3f
               && std::abs (rate->convertFrom0to1 (0.5f) - 1.0f) < 0.01f,
           "the LFO rate runs from 0.05 to 20 Hz, with 1 Hz in the middle of the knob");
    check (rate->getText (rate->convertTo0to1 (5.0f), 20) == "5.00 Hz"
               && rate->getText (rate->convertTo0to1 (0.25f), 20) == "0.25 Hz"
               && std::abs (rate->getValueForText ("2.5 Hz") - rate->convertTo0to1 (2.5f)) < 1.0e-5f,
           "the LFO rate displays in hertz ('5.00 Hz', '0.25 Hz') and accepts '2.5 Hz'");
    check (std::abs (depth->convertFrom0to1 (1.0f) - 100.0f) < 0.01f && depth->getText (depth->convertTo0to1 (50.0f), 20) == "50",
           "the LFO depth runs from 0 to 100 % and displays as a number ('50')");

    // Spelled out on purpose, like the IDs above.
    auto* destination = findParameter (*processor, "lfoDestination");
    check (destination != nullptr, "parameter ID 'lfoDestination' exists");

    if (destination == nullptr)
        return;

    const juce::StringArray expected { "Pitch", "Volume", "Filter Cutoff" };
    bool inOrder = true;

    for (int i = 0; i < expected.size(); ++i)
        inOrder = inOrder && destination->getText (destination->convertTo0to1 ((float) i), 20) == expected[i];

    check (getParameter (*processor, "lfoDestination") == 0.0f, "the LFO destination defaults to the first choice (pitch)");
    check (inOrder, "the LFO destinations are Pitch, Volume and Filter Cutoff, in that order");
}

void testStateRoundTrip()
{
    auto original = createProcessor();
    setParameter (*original, "volume", -12.0f);
    setParameter (*original, "velocitySensitivity", 75.0f);
    setParameter (*original, "pitchBendRange", 7.0f);
    setParameter (*original, "waveform", 2.0f);
    setParameter (*original, "ampAttack", 0.03f);
    setParameter (*original, "ampDecay", 1.25f);
    setParameter (*original, "ampSustain", 40.0f);
    setParameter (*original, "ampRelease", 3.0f);
    setParameter (*original, "filterEnabled", 1.0f);
    setParameter (*original, "filterCutoff", 750.0f);
    setParameter (*original, "filterResonance", 65.0f);
    setParameter (*original, "filterType", 1.0f);
    setParameter (*original, "filterKeyTracking", 80.0f);
    setParameter (*original, "filterAttack", 0.02f);
    setParameter (*original, "filterDecay", 0.8f);
    setParameter (*original, "filterSustain", 30.0f);
    setParameter (*original, "filterRelease", 1.5f);
    setParameter (*original, "filterEnvAmount", -2.5f);
    setParameter (*original, "filterVelocity", 3.25f);
    setParameter (*original, "lfoRate", 0.7f);
    setParameter (*original, "lfoDepth", 35.0f);
    setParameter (*original, "lfoDestination", 2.0f);

    juce::MemoryBlock state;
    original->getStateInformation (state);

    auto restored = createProcessor();
    restored->setStateInformation (state.getData(), (int) state.getSize());

    const auto restoredVolume = getParameter (*restored, "volume");
    const auto restoredSensitivity = getParameter (*restored, "velocitySensitivity");
    const auto restoredRange = getParameter (*restored, "pitchBendRange");
    std::printf ("      restored volume = %.2f dB (expected -12.00), sensitivity = %.1f %% (expected 75.0), "
                 "bend range = %.0f (expected 7)\n",
                 restoredVolume, restoredSensitivity, restoredRange);
    check (std::abs (restoredVolume - (-12.0f)) < 0.01f, "volume survives saving and reloading");
    check (std::abs (restoredSensitivity - 75.0f) < 0.01f, "velocity sensitivity survives saving and reloading");
    check (std::abs (restoredRange - 7.0f) < 0.01f, "pitch bend range survives saving and reloading");
    check (std::abs (getParameter (*restored, "waveform") - 2.0f) < 0.01f, "the waveform choice (Saw) survives saving and reloading");
    check (std::abs (getParameter (*restored, "ampAttack") - 0.03f) < 0.0001f
               && std::abs (getParameter (*restored, "ampDecay") - 1.25f) < 0.0001f
               && std::abs (getParameter (*restored, "ampSustain") - 40.0f) < 0.01f
               && std::abs (getParameter (*restored, "ampRelease") - 3.0f) < 0.0001f,
           "the amp envelope survives saving and reloading");
    check (getParameter (*restored, "filterEnabled") == 1.0f
               && std::abs (getParameter (*restored, "filterCutoff") - 750.0f) < 0.5f
               && std::abs (getParameter (*restored, "filterResonance") - 65.0f) < 0.01f
               && getParameter (*restored, "filterType") == 1.0f
               && std::abs (getParameter (*restored, "filterKeyTracking") - 80.0f) < 0.01f,
           "the filter settings (including its type, high-pass, and key tracking) survive saving and reloading");
    check (std::abs (getParameter (*restored, "filterAttack") - 0.02f) < 0.0001f
               && std::abs (getParameter (*restored, "filterDecay") - 0.8f) < 0.0001f
               && std::abs (getParameter (*restored, "filterSustain") - 30.0f) < 0.01f
               && std::abs (getParameter (*restored, "filterRelease") - 1.5f) < 0.0001f
               && std::abs (getParameter (*restored, "filterEnvAmount") + 2.5f) < 0.001f,
           "the filter envelope survives saving and reloading");
    check (std::abs (getParameter (*restored, "filterVelocity") - 3.25f) < 0.001f, "Filter Velocity survives saving and reloading");
    check (std::abs (getParameter (*restored, "lfoRate") - 0.7f) < 1.0e-4f && std::abs (getParameter (*restored, "lfoDepth") - 35.0f) < 0.01f,
           "the LFO's rate and depth survive saving and reloading");
    check (getParameter (*restored, "lfoDestination") == 2.0f, "the LFO destination (Filter Cutoff) survives saving and reloading");

    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    check (xml != nullptr && xml->getIntAttribute ("stateVersion") == 1, "saved state has version 1");
}

void testStateFromOlderVersions()
{
    // Build the state a project saved when Volume was the only parameter would hold: a real saved
    // state with the entries for the later parameters removed.
    auto original = createProcessor();
    setParameter (*original, "volume", -12.0f);
    setParameter (*original, "velocitySensitivity", 75.0f);
    setParameter (*original, "pitchBendRange", 7.0f);
    setParameter (*original, "waveform", 2.0f);
    setParameter (*original, "ampAttack", 0.03f);
    setParameter (*original, "ampSustain", 40.0f);
    setParameter (*original, "ampRelease", 3.0f);
    setParameter (*original, "filterEnabled", 1.0f);
    setParameter (*original, "filterCutoff", 750.0f);
    setParameter (*original, "filterType", 2.0f);
    setParameter (*original, "filterKeyTracking", 80.0f);
    setParameter (*original, "filterEnvAmount", 3.0f);
    setParameter (*original, "filterVelocity", 5.0f);
    setParameter (*original, "lfoRate", 0.7f);
    setParameter (*original, "lfoDepth", 35.0f);
    setParameter (*original, "lfoDestination", 1.0f);

    juce::MemoryBlock state;
    original->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());

    if (xml == nullptr)
    {
        check (false, "saved state is readable XML");
        return;
    }

    for (auto* id : { "velocitySensitivity", "pitchBendRange", "waveform", "ampAttack", "ampDecay", "ampSustain", "ampRelease",
                      "filterEnabled", "filterCutoff", "filterResonance", "filterType", "filterKeyTracking",
                      "filterAttack", "filterDecay", "filterSustain", "filterRelease", "filterEnvAmount",
                      "filterVelocity", "lfoRate", "lfoDepth", "lfoDestination" })
        if (auto* entry = xml->getChildByAttribute ("id", id))
            xml->removeChildElement (entry, true);

    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary (*xml, oldState);

    auto loaded = createProcessor();
    loaded->setStateInformation (oldState.getData(), (int) oldState.getSize());

    check (std::abs (getParameter (*loaded, "volume") - (-12.0f)) < 0.01f,
           "an older project keeps its volume");
    check (std::abs (getParameter (*loaded, "velocitySensitivity")) < 0.01f,
           "an older project loads with velocity sensitivity at 0 % (velocity ignored, as before)");
    check (std::abs (getParameter (*loaded, "pitchBendRange") - 2.0f) < 0.01f,
           "an older project loads with the default pitch bend range of 2 semitones");
    check (std::abs (getParameter (*loaded, "waveform")) < 0.01f,
           "an older project loads with the sine, its only waveform back then");
    check (getParameter (*loaded, "ampAttack") == 0.0f
               && std::abs (getParameter (*loaded, "ampSustain") - 100.0f) < 0.01f
               && std::abs (getParameter (*loaded, "ampRelease") - 0.005f) < 1.0e-6f,
           "an older project loads with the envelope that reproduces its old sound");
    check (getParameter (*loaded, "filterEnabled") == 0.0f && std::abs (getParameter (*loaded, "filterCutoff") - 20000.0f) < 0.5f,
           "an older project loads with the filter off");
    check (getParameter (*loaded, "filterType") == 0.0f, "an older project loads with the low-pass filter type, its only type back then");
    check (getParameter (*loaded, "filterKeyTracking") == 0.0f, "an older project loads with no key tracking");
    check (getParameter (*loaded, "filterEnvAmount") == 0.0f, "an older project loads with the filter envelope having no effect");
    check (getParameter (*loaded, "filterVelocity") == 0.0f, "an older project loads with velocity not affecting the filter");
    check (std::abs (getParameter (*loaded, "lfoRate") - 5.0f) < 1.0e-4f && getParameter (*loaded, "lfoDepth") == 0.0f,
           "an older project loads with the LFO at 5 Hz and no depth (no effect)");
    check (getParameter (*loaded, "lfoDestination") == 0.0f, "an older project loads with the LFO on the pitch, its only destination back then");
}

void testOldAndInvalidState()
{
    // A project saved before the plug-in had parameters holds an empty state.
    auto processor = createProcessor();
    setParameter (*processor, "volume", -12.0f);
    processor->setStateInformation (nullptr, 0);
    check (std::abs (getParameter (*processor, "volume") - (-12.0f)) < 0.01f,
           "empty state (projects from before parameters) is ignored");

    const char garbage[] = "definitely not a saved state";
    processor->setStateInformation (garbage, (int) sizeof (garbage));
    check (std::abs (getParameter (*processor, "volume") - (-12.0f)) < 0.01f, "garbage state is ignored");

    juce::MemoryBlock wrongTag;
    juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("SomethingElse"), wrongTag);
    processor->setStateInformation (wrongTag.getData(), (int) wrongTag.getSize());
    check (std::abs (getParameter (*processor, "volume") - (-12.0f)) < 0.01f,
           "state from something other than this plug-in is ignored");
}

} // namespace

void runParameterTests()
{
    testVolumeParameter();
    testVolumeText();
    testVelocitySensitivityParameter();
    testPitchBendRangeParameter();
    testWaveformParameter();
    testAmpEnvelopeParameters();
    testFilterParameters();
    testFilterEnvelopeParameters();
    testFilterVelocityParameter();
    testLfoParameters();
    testStateRoundTrip();
    testStateFromOlderVersions();
    testOldAndInvalidState();
}

} // namespace testing
