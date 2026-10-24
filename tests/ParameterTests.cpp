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

void testStateRoundTrip()
{
    auto original = createProcessor();
    setParameter (*original, "volume", -12.0f);

    juce::MemoryBlock state;
    original->getStateInformation (state);

    auto restored = createProcessor();
    restored->setStateInformation (state.getData(), (int) state.getSize());

    const auto restoredVolume = getParameter (*restored, "volume");
    std::printf ("      restored volume = %.2f dB (expected -12.00)\n", restoredVolume);
    check (std::abs (restoredVolume - (-12.0f)) < 0.01f, "volume survives saving and reloading");

    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    check (xml != nullptr && xml->getIntAttribute ("stateVersion") == 1, "saved state has version 1");
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
    testStateRoundTrip();
    testOldAndInvalidState();
}
} // namespace testing
