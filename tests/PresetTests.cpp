#include "TestHelpers.h"

#include "BoringSynthProcessor.h"
#include "FactoryPresets.h"

// Preset tests: the factory presets, saving and loading your own, the preset saved with a project,
// and the factory presets as the host's programs. Presets are saved to a temporary folder, never
// your Documents.

namespace testing
{

namespace
{

using boringsynth::BoringSynthProcessor;

std::unique_ptr<BoringSynthProcessor> createSynth()
{
    return std::unique_ptr<BoringSynthProcessor> (dynamic_cast<BoringSynthProcessor*> (createPluginFilter()));
}

// A folder for presets, empty to start with and deleted afterwards.
struct TemporaryPresetFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("BoringSynthPresetTests-" + juce::String (juce::Random::getSystemRandom().nextInt64()));

    ~TemporaryPresetFolder() { folder.deleteRecursively(); }
};

juce::RangedAudioParameter& parameterOf (juce::AudioProcessor& processor, const juce::String& id)
{
    return *findParameter (processor, id);
}

// Moves every parameter away from its default, to values that differ from parameter to parameter.
void scrambleParameters (juce::AudioProcessor& processor, float seed = 0.3f)
{
    int i = 0;

    for (auto* parameter : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        auto value = std::fmod (seed + 0.37f * (float) i++, 1.0f);

        if (std::abs (value - ranged->getDefaultValue()) < 0.2f)
            value = std::fmod (value + 0.5f, 1.0f);

        ranged->setValueNotifyingHost (ranged->convertTo0to1 (ranged->convertFrom0to1 (value)));
    }
}

// Every parameter's value (0 to 1), in order.
std::vector<float> valuesOf (juce::AudioProcessor& processor)
{
    std::vector<float> values;

    for (auto* parameter : processor.getParameters())
        values.push_back (parameter->getValue());

    return values;
}

bool sameValues (const std::vector<float>& a, const std::vector<float>& b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs (a[i] - b[i]) > 1.0e-5f)
            return false;

    return a.size() == b.size();
}

// The parameters that aren't at their default.
juce::StringArray parametersAwayFromDefault (juce::AudioProcessor& processor)
{
    juce::StringArray ids;

    for (auto* parameter : processor.getParameters())
        if (std::abs (parameter->getValue() - parameter->getDefaultValue()) > 1.0e-5f)
            ids.add (dynamic_cast<juce::RangedAudioParameter*> (parameter)->getParameterID());

    return ids;
}

//==============================================================================
void testFactoryPresetsAreValid()
{
    // Init first, setting nothing (every parameter at its default); names all different; and every
    // setting a real parameter, within its range.
    auto synth = createSynth();
    const auto& factory = boringsynth::getFactoryPresets();
    juce::StringArray names, problems;

    for (const auto& preset : factory)
    {
        if (names.contains (preset.name) || juce::String (preset.name).isEmpty())
            problems.add (juce::String ("name '") + preset.name + "'");

        names.add (preset.name);

        for (const auto& [id, value] : preset.settings)
        {
            auto* parameter = findParameter (*synth, id);
            const auto range = parameter != nullptr ? parameter->getNormalisableRange() : juce::NormalisableRange<float>();

            if (parameter == nullptr || value < range.start || value > range.end)
                problems.add (juce::String (preset.name) + ": " + id);
        }
    }

    std::printf ("      %d factory presets: %s\n", names.size(), names.joinIntoString (", ").toRawUTF8());

    if (! problems.isEmpty())
        std::printf ("      problems: %s\n", problems.joinIntoString ("; ").toRawUTF8());

    check (factory.size() > 1 && juce::String (factory.front().name) == "Init" && factory.front().settings.empty(),
           "the first factory preset is Init, which leaves every parameter at its default");
    check (problems.isEmpty(), "every factory preset has its own name, and sets only real parameters, within their ranges");
}

void testInitSoundsLikeANewPlugIn()
{
    // After moving every parameter, Init puts them all back: the output is exactly a new plug-in's.
    auto synth = createSynth();
    scrambleParameters (*synth);
    synth->getPresetManager().loadFactoryPreset (0);
    const auto stillMoved = parametersAwayFromDefault (*synth);

    const std::vector<TimedMessage> events { { 0, noteOn (60) }, { 9000, noteOn (64, 70) }, { 30000, noteOff (60) } };
    const auto fresh = render ({ .events = events });
    const auto afterInit = render ({ .events = events,
                                     .setUp = [] (juce::AudioProcessor& p)
                                     {
                                         scrambleParameters (p, 0.6f);
                                         dynamic_cast<BoringSynthProcessor&> (p).getPresetManager().loadFactoryPreset (0);
                                     } });
    check (stillMoved.isEmpty() && fresh == afterInit, "Init puts every parameter back to its default: it sounds exactly like a new plug-in");
}

void testFactoryPresetSetsItsSettings()
{
    // A factory preset sets the parameters it lists, and puts every other one back to its default.
    auto synth = createSynth();
    scrambleParameters (*synth);
    auto& presets = synth->getPresetManager();
    const auto index = 2;
    const auto& preset = boringsynth::getFactoryPresets()[(size_t) index];
    presets.loadFactoryPreset (index);

    bool listedRight = true;
    juce::StringArray listed;

    for (const auto& [id, value] : preset.settings)
    {
        listed.add (id);
        listedRight = listedRight && std::abs (getParameter (*synth, id) - value) < 1.0e-3f * std::max (1.0f, std::abs (value));
    }

    bool othersAtDefault = true;

    for (const auto& id : parametersAwayFromDefault (*synth))
        othersAtDefault = othersAtDefault && listed.contains (id);

    check (listedRight && othersAtDefault, "a factory preset sets what it lists, and everything else to its default");
    check (presets.getCurrentPresetName() == preset.name && presets.getCurrentFactoryPreset() == index,
           "loading a factory preset makes it the current preset");
}

void testUserPresetRoundTrip()
{
    // Save every setting as a preset, change them all, load the preset: everything comes back.
    TemporaryPresetFolder temporary;
    auto synth = createSynth();
    auto& presets = synth->getPresetManager();
    presets.setUserPresetFolder (temporary.folder);
    scrambleParameters (*synth);
    const auto saved = valuesOf (*synth);

    const auto savedOk = presets.saveUserPreset ("My Sound");
    const auto listed = presets.getUserPresetNames();
    presets.loadFactoryPreset (0);
    const auto loadedOk = presets.loadUserPreset ("My Sound");

    std::printf ("      saved to %s\n", temporary.folder.getChildFile ("My Sound.xml").getFullPathName().toRawUTF8());
    check (savedOk && temporary.folder.getChildFile ("My Sound.xml").existsAsFile() && listed == juce::StringArray ("My Sound"),
           "saving a preset writes it to the presets folder, and it's listed");
    check (loadedOk && sameValues (valuesOf (*synth), saved), "loading your preset brings back every setting");
    check (presets.getCurrentPresetName() == "My Sound" && presets.getCurrentFactoryPreset() == -1,
           "loading your preset makes it the current preset");
}

void testOlderPresetFillsInDefaults()
{
    // A preset saved before some parameters existed doesn't hold them: they load at their defaults,
    // as an older project's do.
    TemporaryPresetFolder temporary;
    auto synth = createSynth();
    auto& presets = synth->getPresetManager();
    presets.setUserPresetFolder (temporary.folder);
    scrambleParameters (*synth);

    auto xml = presets.createPresetXml();

    for (auto* id : { "lfoSync", "lfoRetrigger", "modWheelLfoDepth" })
        xml->removeChildElement (xml->getChildByAttribute ("id", id), true);

    temporary.folder.createDirectory();
    xml->writeTo (temporary.folder.getChildFile ("Old.xml"));
    const auto volumeInFile = getParameter (*synth, "volume");

    scrambleParameters (*synth, 0.8f);
    const auto loaded = presets.loadUserPreset ("Old");
    const auto missingAtDefault = parameterOf (*synth, "lfoSync").getValue() == parameterOf (*synth, "lfoSync").getDefaultValue()
                               && parameterOf (*synth, "lfoRetrigger").getValue() == parameterOf (*synth, "lfoRetrigger").getDefaultValue()
                               && parameterOf (*synth, "modWheelLfoDepth").getValue() == parameterOf (*synth, "modWheelLfoDepth").getDefaultValue();
    check (loaded && missingAtDefault && std::abs (getParameter (*synth, "volume") - volumeInFile) < 0.01f,
           "a preset missing some parameters loads them at their defaults, and the rest from the file");
}

void testBadPresetFilesAreRefused()
{
    // A file that isn't a BoringSynth preset, or is from a newer version, is refused and changes
    // nothing: not the settings, not the current preset.
    TemporaryPresetFolder temporary;
    auto synth = createSynth();
    auto& presets = synth->getPresetManager();
    presets.setUserPresetFolder (temporary.folder);
    temporary.folder.createDirectory();
    temporary.folder.getChildFile ("Garbage.xml").replaceWithText ("definitely not a preset");
    temporary.folder.getChildFile ("Other.xml").replaceWithText ("<SomethingElse/>");
    auto future = presets.createPresetXml();
    future->setAttribute ("stateVersion", 99);
    future->writeTo (temporary.folder.getChildFile ("Future.xml"));

    scrambleParameters (*synth);
    presets.loadFactoryPreset (3);
    const auto before = valuesOf (*synth);
    const auto refused = ! presets.loadUserPreset ("Garbage") && ! presets.loadUserPreset ("Other")
                      && ! presets.loadUserPreset ("Future") && ! presets.loadUserPreset ("Not There");
    check (refused && sameValues (valuesOf (*synth), before) && presets.getCurrentFactoryPreset() == 3,
           "a file that isn't a preset, or is from a newer version, is refused, changing nothing");
}

void testPresetNamesAreMadeSafe()
{
    // A name with characters a file name can't have is still saved, and loads back by the same
    // name; an empty name isn't saved.
    TemporaryPresetFolder temporary;
    auto synth = createSynth();
    auto& presets = synth->getPresetManager();
    presets.setUserPresetFolder (temporary.folder);
    const auto odd = juce::String ("Bass/Lead: v2?");
    const auto saved = presets.saveUserPreset (odd);
    const auto names = presets.getUserPresetNames();
    std::printf ("      '%s' saved as '%s'\n", odd.toRawUTF8(), names.joinIntoString (", ").toRawUTF8());
    check (saved && names.size() == 1 && presets.userPresetExists (odd) && presets.loadUserPreset (odd),
           "a preset name with characters file names can't have is saved, and loads back");
    check (! presets.saveUserPreset ("   ") && presets.getUserPresetNames().size() == 1, "an empty preset name isn't saved");
}

void testUserPresetList()
{
    // Only .xml files count, listed in alphabetical order whatever their case.
    TemporaryPresetFolder temporary;
    auto synth = createSynth();
    auto& presets = synth->getPresetManager();
    presets.setUserPresetFolder (temporary.folder);
    check (presets.getUserPresetNames().isEmpty() && ! temporary.folder.exists(),
           "with no presets saved, none are listed, and the folder isn't created");

    for (auto* name : { "banjo", "Accordion", "cello" })
        presets.saveUserPreset (name);

    temporary.folder.getChildFile ("notes.txt").replaceWithText ("not a preset");
    const auto names = presets.getUserPresetNames();
    check (names == juce::StringArray ("Accordion", "banjo", "cello"), "your presets are listed alphabetically, and only .xml files");
}

void testPresetSavedWithTheProject()
{
    // A project reopens on the preset it was on, factory or yours. One saved before presets existed
    // shows no preset name.
    TemporaryPresetFolder temporary;
    auto reopen = [] (BoringSynthProcessor& from)
    {
        juce::MemoryBlock state;
        from.getStateInformation (state);
        auto to = createSynth();
        to->setStateInformation (state.getData(), (int) state.getSize());
        return to;
    };

    auto synth = createSynth();
    synth->getPresetManager().setUserPresetFolder (temporary.folder);
    synth->getPresetManager().loadFactoryPreset (4);
    auto reopenedFactory = reopen (*synth);
    synth->getPresetManager().saveUserPreset ("Mine");
    auto reopenedUser = reopen (*synth);

    check (reopenedFactory->getPresetManager().getCurrentPresetName() == boringsynth::getFactoryPresets()[4].name
               && reopenedFactory->getPresetManager().getCurrentFactoryPreset() == 4
               && reopenedUser->getPresetManager().getCurrentPresetName() == "Mine"
               && reopenedUser->getPresetManager().getCurrentFactoryPreset() == -1,
           "a project reopens on the preset it was on");

    juce::MemoryBlock state;
    synth->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    xml->removeAttribute ("presetName");
    xml->removeAttribute ("factoryPreset");
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary (*xml, oldState);
    auto old = createSynth();
    old->setStateInformation (oldState.getData(), (int) oldState.getSize());
    check (old->getPresetManager().getCurrentPresetName().isEmpty() && old->getPresetManager().getCurrentFactoryPreset() == -1
               && old->getCurrentProgram() == old->getNumPrograms() - 1,
           "a project saved before presets existed shows no preset name, and the host sees the user-preset program");
}

void testFactoryPresetsAsPrograms()
{
    // The host's program list is the factory presets, then one standing for anything else. Choosing
    // a factory preset loads it, but choosing the one that's already current does nothing: some hosts
    // set it again when reopening a project, which mustn't throw away the changes saved with it.
    auto synth = createSynth();
    const auto numFactoryPresets = (int) boringsynth::getFactoryPresets().size();
    bool namesMatch = synth->getNumPrograms() == numFactoryPresets + 1
                   && synth->getProgramName (numFactoryPresets) == BoringSynthProcessor::userPresetProgramName;

    for (int i = 0; namesMatch && i < numFactoryPresets; ++i)
        namesMatch = synth->getProgramName (i) == boringsynth::getFactoryPresets()[(size_t) i].name;

    check (namesMatch && synth->getCurrentProgram() == 0,
           "the host's programs are the factory presets and then the user-preset program, starting on Init");

    synth->setCurrentProgram (2);
    const auto loaded = synth->getCurrentProgram() == 2 && synth->getPresetManager().getCurrentPresetName() == "Pluck Bass";
    setParameter (*synth, "filterCutoff", 777.0f);

    juce::MemoryBlock state;
    synth->getStateInformation (state);
    auto reopened = createSynth();
    reopened->setStateInformation (state.getData(), (int) state.getSize());
    reopened->setCurrentProgram (reopened->getCurrentProgram());
    synth->setCurrentProgram (2);
    const auto changesKept = std::abs (getParameter (*synth, "filterCutoff") - 777.0f) < 0.5f
                          && std::abs (getParameter (*reopened, "filterCutoff") - 777.0f) < 0.5f;

    synth->setCurrentProgram (0);
    check (loaded, "choosing a program loads that factory preset");
    check (changesKept, "choosing the program that's already current (as a host may on reopening) keeps the changes made since");
    check (parametersAwayFromDefault (*synth).isEmpty(), "choosing another program loads it");
}

void testUserPresetThenInitProgram()
{
    // On a user preset the host sees the user-preset program, not Init's, so choosing Init from the
    // host loads it. Setting the user-preset program again, as a host may on reopening a project that
    // was on a user preset, keeps its settings.
    TemporaryPresetFolder temporary;
    auto synth = createSynth();
    auto& presets = synth->getPresetManager();
    presets.setUserPresetFolder (temporary.folder);
    setParameter (*synth, "filterCutoff", 777.0f);
    presets.saveUserPreset ("Mine");
    const auto userPresetProgram = synth->getNumPrograms() - 1;
    const auto onUserProgram = synth->getCurrentProgram() == userPresetProgram;

    juce::MemoryBlock state;
    synth->getStateInformation (state);
    auto reopened = createSynth();
    reopened->setStateInformation (state.getData(), (int) state.getSize());
    reopened->setCurrentProgram (reopened->getCurrentProgram());
    synth->setCurrentProgram (userPresetProgram);
    const auto kept = std::abs (getParameter (*reopened, "filterCutoff") - 777.0f) < 0.5f
                   && std::abs (getParameter (*synth, "filterCutoff") - 777.0f) < 0.5f
                   && presets.getCurrentPresetName() == "Mine";

    synth->setCurrentProgram (0);
    const auto initLoaded = parametersAwayFromDefault (*synth).isEmpty() && synth->getCurrentProgram() == 0
                         && presets.getCurrentPresetName() == "Init";

    check (onUserProgram, "on a user preset, the host's current program is the user-preset program");
    check (kept, "setting the user-preset program (as a host may on reopening) keeps the settings");
    check (initLoaded, "choosing Init from the host after a user preset loads Init");
}

void testSubBass()
{
    // Sub Bass is a pure sine: played at full velocity, exactly what Init (a plain sine) plays while
    // the key is held. Released, it fades over 120 ms rather than Init's 5 ms; played softly, it's a
    // little quieter (30 % velocity sensitivity: velocity 64 is about 0.78 of full level).
    int subBass = -1;

    for (int i = 0; i < (int) boringsynth::getFactoryPresets().size(); ++i)
        if (juce::String (boringsynth::getFactoryPresets()[(size_t) i].name) == "Sub Bass")
            subBass = i;

    check (subBass == (int) boringsynth::getFactoryPresets().size() - 1, "Sub Bass is the last factory preset, so the others keep their positions");

    auto withPreset = [] (int index)
    {
        return [index] (juce::AudioProcessor& p) { dynamic_cast<BoringSynthProcessor&> (p).getPresetManager().loadFactoryPreset (index); };
    };
    constexpr int noteOffAt = 24000;
    const std::vector<TimedMessage> fullVelocity { { 0, noteOn (33, 127) }, { noteOffAt, noteOff (33) } }; // A1, 55 Hz
    const auto init = render ({ .events = fullVelocity, .setUp = withPreset (0) });
    const auto sub = render ({ .events = fullVelocity, .setUp = withPreset (subBass) });
    const auto soft = render ({ .events = { { 0, noteOn (33, 64) } }, .setUp = withPreset (subBass) });

    const auto heldSame = std::equal (init.begin(), init.begin() + noteOffAt, sub.begin());
    const auto fadeMs = 1000.0 * (lastNonZeroIndex (sub) - noteOffAt) / testSampleRate;
    const auto softLevel = peakBetween (soft, 4800, 24000) / peakBetween (sub, 4800, 24000);
    std::printf ("      Sub Bass: fades over %.1f ms after release; velocity 64 plays at %.3f of full level\n", fadeMs, softLevel);
    check (heldSame, "Sub Bass plays a pure sine, exactly as Init does while the key is held");
    check (std::abs (fadeMs - 120.0) < 1.0 && std::abs (softLevel - (0.7 + 0.3 * std::pow (64.0 / 127.0, 2.0))) < 0.01,
           "Sub Bass fades over 120 ms when released, and is a little quieter played softly");
}

} // namespace

void runPresetTests()
{
    testFactoryPresetsAreValid();
    testInitSoundsLikeANewPlugIn();
    testFactoryPresetSetsItsSettings();
    testUserPresetRoundTrip();
    testOlderPresetFillsInDefaults();
    testBadPresetFilesAreRefused();
    testPresetNamesAreMadeSafe();
    testUserPresetList();
    testPresetSavedWithTheProject();
    testFactoryPresetsAsPrograms();
    testUserPresetThenInitProgram();
    testSubBass();
}

} // namespace testing
