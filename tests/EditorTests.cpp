#include "TestHelpers.h"

#include "BoringSynthProcessor.h"

// Editor tests: the plug-in's window opens, closes and opens again, and makes no difference to the
// sound; every parameter has a control of the right kind, connected both ways; and the controls fit.

namespace testing
{

namespace
{

// Closes the plug-in's editor the way JUCE's plug-in wrappers do when the host closes its window:
// telling the processor first, then deleting it.
void closeEditor (juce::AudioProcessor& processor)
{
    if (auto* editor = processor.getActiveEditor())
    {
        processor.editorBeingDeleted (editor);
        delete editor;
    }
}

void testEditorOpens()
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    check (processor->hasEditor(), "the plug-in says it has an editor");

    auto* editor = processor->createEditorAndMakeActive();
    check (editor != nullptr && editor->getWidth() > 0 && editor->getHeight() > 0, "its editor opens, with a size");
    check (processor->getActiveEditor() == editor, "the plug-in knows its editor is open");

    // Hosts close the editor when its window closes, and ask for a new one when it's opened again.
    closeEditor (*processor);
    check (processor->getActiveEditor() == nullptr, "closing the editor leaves no editor open");

    auto* reopened = processor->createEditorAndMakeActive();
    check (reopened != nullptr && processor->getActiveEditor() == reopened, "the editor opens again after closing");
    closeEditor (*processor);
}

void testEditorDoesNotChangeTheSound()
{
    // Rendering with the editor open gives exactly the same output as without it.
    const std::vector<TimedMessage> events { { 0, noteOn (60) }, { 9000, noteOn (67) }, { 30000, noteOff (60) } };
    const auto withoutEditor = render ({ .events = events });
    const auto withEditor = render ({ .events = events,
                                      .setUp = [] (juce::AudioProcessor& p) { p.createEditorAndMakeActive(); },
                                      .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                      {
                                          // Closed before the processor goes, as a host would close it.
                                          if (blockStart + 512 >= 48000)
                                              closeEditor (p);
                                      } });
    check (withoutEditor == withEditor, "having the editor open makes no difference to the sound");
}

//==============================================================================
// The editor's controls. Each one's component ID is its parameter's ID.

// A processor with its editor open, closed again when this goes.
struct OpenEditor
{
    std::unique_ptr<juce::AudioProcessor> processor { createPluginFilter() };
    juce::AudioProcessorEditor* editor = processor->createEditorAndMakeActive();

    ~OpenEditor() { closeEditor (*processor); }

    juce::Component* control (const juce::String& parameterId) const { return editor->findChildWithID (parameterId); }

    template <typename ControlType>
    ControlType* control (const juce::String& parameterId) const { return dynamic_cast<ControlType*> (control (parameterId)); }
};

void testEveryParameterHasAControl()
{
    // One control each, of the right kind: a menu with the parameter's choices, a tick box for a
    // switch, or a knob with the parameter's range.
    OpenEditor open;
    juce::StringArray missing, wrong;

    for (auto* parameter : open.processor->getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        const auto id = ranged->getParameterID();
        int found = 0;

        for (auto* child : open.editor->getChildren())
            if (child->getComponentID() == id)
                ++found;

        if (found != 1)
        {
            missing.add (id + " (" + juce::String (found) + ")");
            continue;
        }

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
        {
            auto* menu = open.control<juce::ComboBox> (id);
            bool sameChoices = menu != nullptr && menu->getNumItems() == choice->choices.size();

            for (int i = 0; sameChoices && i < choice->choices.size(); ++i)
                sameChoices = menu->getItemText (i) == choice->choices[i];

            if (! sameChoices)
                wrong.add (id);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr)
        {
            if (open.control<juce::ToggleButton> (id) == nullptr)
                wrong.add (id);
        }
        else
        {
            auto* knob = open.control<juce::Slider> (id);
            const auto range = ranged->getNormalisableRange();

            if (knob == nullptr || knob->getMinimum() != range.start || knob->getMaximum() != range.end)
                wrong.add (id);
        }
    }

    if (! missing.isEmpty() || ! wrong.isEmpty())
        std::printf ("      not exactly one control: %s; the wrong kind of control: %s\n",
                     missing.joinIntoString (", ").toRawUTF8(), wrong.joinIntoString (", ").toRawUTF8());

    check (missing.isEmpty(), "every parameter has exactly one control in the editor");
    check (wrong.isEmpty(), "each control is the right kind: a menu of its choices, a tick box, or a knob over its range");
}

void testControlsMoveParameters()
{
    // Turning a knob, picking from a menu or ticking a box sets its parameter.
    OpenEditor open;
    auto* knob = open.control<juce::Slider> ("filterCutoff");
    auto* menu = open.control<juce::ComboBox> ("lfoShape");
    auto* tickBox = open.control<juce::ToggleButton> ("filterEnabled");

    if (knob == nullptr || menu == nullptr || tickBox == nullptr)
    {
        check (false, "the controls to move exist");
        return;
    }

    knob->setValue (1000.0, juce::sendNotificationSync);
    menu->setSelectedItemIndex (3, juce::sendNotificationSync);
    tickBox->setToggleState (true, juce::sendNotificationSync);
    check (std::abs (getParameter (*open.processor, "filterCutoff") - 1000.0f) < 0.5f
               && getParameter (*open.processor, "lfoShape") == 3.0f
               && getParameter (*open.processor, "filterEnabled") == 1.0f,
           "moving a knob, menu or tick box sets its parameter");
}

void testParametersMoveControls()
{
    // When the host changes a parameter (automation, or loading a project), its control follows.
    OpenEditor open;
    auto* knob = open.control<juce::Slider> ("ampRelease");
    auto* menu = open.control<juce::ComboBox> ("waveform");
    auto* tickBox = open.control<juce::ToggleButton> ("lfoRetrigger");
    setParameter (*open.processor, "ampRelease", 1.5f);
    setParameter (*open.processor, "waveform", 2.0f);
    setParameter (*open.processor, "lfoRetrigger", 1.0f);
    check (knob != nullptr && menu != nullptr && tickBox != nullptr
               && std::abs (knob->getValue() - 1.5) < 1.0e-4
               && menu->getSelectedItemIndex() == 2
               && tickBox->getToggleState(),
           "when a parameter changes, its control follows");
}

void testKnobsShowTheParametersText()
{
    // Knobs show the parameter's own value text, with its unit when it keeps that separately.
    OpenEditor open;
    auto textOf = [&] (const char* id, double value)
    {
        auto* knob = open.control<juce::Slider> (id);
        return knob != nullptr ? knob->getTextFromValue (value) : juce::String ("(no knob)");
    };
    const auto cutoff = textOf ("filterCutoff", 632.46);
    const auto volume = textOf ("volume", -12.0);
    const auto velocity = textOf ("velocitySensitivity", 50.0);
    const auto envAmount = textOf ("filterEnvAmount", 2.0);
    std::printf ("      knobs show: '%s', '%s', '%s', '%s'\n", cutoff.toRawUTF8(), volume.toRawUTF8(), velocity.toRawUTF8(), envAmount.toRawUTF8());
    check (cutoff == "632 Hz" && volume == "-12.0 dB" && velocity == "50 %" && envAmount == "+2.00 oct",
           "knobs show their parameter's value text, with its unit");
}

void testSyncGreysOutTheRateNotInUse()
{
    // Only one of LFO Rate and LFO Sync Rate sets the LFO's speed, so the other is greyed out.
    OpenEditor open;
    auto* rate = open.control ("lfoRate");
    auto* syncRate = open.control ("lfoSyncRate");

    if (rate == nullptr || syncRate == nullptr)
    {
        check (false, "the LFO Rate and LFO Sync Rate controls exist");
        return;
    }

    const auto unsyncedRight = rate->isEnabled() && ! syncRate->isEnabled();
    setParameter (*open.processor, "lfoSync", 1.0f);
    const auto syncedRight = ! rate->isEnabled() && syncRate->isEnabled();
    check (unsyncedRight && syncedRight, "LFO Rate is greyed out while synced, and LFO Sync Rate while not");
}

void testControlsFitAndDontOverlap()
{
    OpenEditor open;
    std::vector<juce::Component*> controls;

    for (auto* parameter : open.processor->getParameters())
        if (auto* control = open.control (dynamic_cast<juce::RangedAudioParameter*> (parameter)->getParameterID()))
            controls.push_back (control);

    bool allInside = true, noneOverlap = true;

    for (size_t i = 0; i < controls.size(); ++i)
    {
        allInside = allInside && open.editor->getLocalBounds().contains (controls[i]->getBounds()) && ! controls[i]->getBounds().isEmpty();

        for (size_t j = i + 1; j < controls.size(); ++j)
            noneOverlap = noneOverlap && ! controls[i]->getBounds().intersects (controls[j]->getBounds());
    }

    std::printf ("      editor: %d x %d\n", open.editor->getWidth(), open.editor->getHeight());
    check (allInside && noneOverlap, "every control fits in the window, and none overlap");
}

void testPresetMenu()
{
    // The preset menu lists the factory presets, then yours (here, from a temporary folder), shows the
    // current one, loads the one you pick, and follows a program change from the host.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("BoringSynthEditorTests-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    auto processor = std::unique_ptr<boringsynth::BoringSynthProcessor> (dynamic_cast<boringsynth::BoringSynthProcessor*> (createPluginFilter()));
    auto& presets = processor->getPresetManager();
    presets.setUserPresetFolder (folder);
    presets.saveUserPreset ("Mine");
    presets.dispatchPendingMessages();

    auto* editor = processor->createEditorAndMakeActive();
    auto* menu = dynamic_cast<juce::ComboBox*> (editor->findChildWithID ("presetMenu"));
    auto* save = editor->findChildWithID ("savePreset");

    if (menu == nullptr || save == nullptr)
    {
        check (false, "the editor has a preset menu and a Save button");
        closeEditor (*processor);
        folder.deleteRecursively();
        return;
    }

    juce::StringArray items;

    for (int i = 0; i < menu->getNumItems(); ++i)
        items.add (menu->getItemText (i));

    juce::StringArray expected;

    for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
        expected.add (presets.getFactoryPresetName (i));

    expected.add ("Mine");
    std::printf ("      preset menu: %s (showing '%s')\n", items.joinIntoString (", ").toRawUTF8(), menu->getText().toRawUTF8());
    check (items == expected && menu->getText() == "Mine", "the preset menu lists the factory presets, then yours, and shows the current one");

    menu->setSelectedItemIndex (1, juce::sendNotificationSync);
    presets.dispatchPendingMessages();
    const auto picked = presets.getCurrentPresetName() == presets.getFactoryPresetName (1) && menu->getText() == presets.getFactoryPresetName (1);

    processor->setCurrentProgram (3);
    presets.dispatchPendingMessages();
    const auto followed = menu->getText() == presets.getFactoryPresetName (3);

    check (picked, "picking a preset from the menu loads it");
    check (followed, "the preset menu follows a program change from the host");

    closeEditor (*processor);
    folder.deleteRecursively();
}

} // namespace

void runEditorTests()
{
    testEditorOpens();
    testEditorDoesNotChangeTheSound();
    testEveryParameterHasAControl();
    testControlsMoveParameters();
    testParametersMoveControls();
    testKnobsShowTheParametersText();
    testSyncGreysOutTheRateNotInUse();
    testControlsFitAndDontOverlap();
    testPresetMenu();
}

int saveEditorSnapshot (const char* path)
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    auto* editor = processor->createEditorAndMakeActive();

    if (editor == nullptr)
    {
        std::printf ("the plug-in has no editor to draw\n");
        return 1;
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    closeEditor (*processor);

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (path);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;

    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::printf ("couldn't write %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("drew the editor (%d x %d) to %s\n", image.getWidth(), image.getHeight(), file.getFullPathName().toRawUTF8());
    return 0;
}

} // namespace testing
