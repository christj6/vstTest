#pragma once

#include "PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace boringsynth
{

// The plug-in's window: a preset bar along the top (a menu of the factory presets and yours, and a
// Save button), then every parameter as one of JUCE's standard controls, in labelled groups (Global,
// Oscillator, Amp Envelope, Filter, Filter Envelope, LFO). Knobs for numbers, showing the parameter's
// own value text; menus for choices; tick boxes for switches. Each control is connected to its
// parameter by a JUCE parameter attachment, which keeps the two in step both ways, including when
// the host automates the parameter. A fixed size and JUCE's own look: it's BoringSynth.
//
// Each control's component ID is its parameter's ID, so tests can find them; the preset menu's is
// "presetMenu" and the Save button's "savePreset".
class BoringSynthEditor  : public juce::AudioProcessorEditor,
                           private juce::ChangeListener
{
public:
    // parameters and presets belong to the processor; all three must outlive the editor, as JUCE
    // ensures for a processor's editor.
    BoringSynthEditor (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& parameters, PresetManager& presets);
    ~BoringSynthEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    // The preset menu. It lists the presets afresh each time it opens, so ones saved since (by
    // another copy of the plug-in, say) appear.
    struct PresetMenu  : public juce::ComboBox
    {
        std::function<void()> beforeShowing;

        void showPopup() override
        {
            if (beforeShowing)
                beforeShowing();

            ComboBox::showPopup();
        }
    };

    // One parameter's control: a knob, menu or tick box, its label (knobs and menus only; a tick box
    // shows its own), and the attachment connecting it to the parameter. Declared in this order so
    // the attachment goes before the component it listens to.
    struct Control
    {
        std::unique_ptr<juce::Component> component; // a Slider, ComboBox or ToggleButton
        juce::Label label;
        std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
        std::unique_ptr<juce::ComboBoxParameterAttachment> comboBoxAttachment;
        std::unique_ptr<juce::ButtonParameterAttachment> buttonAttachment;
    };

    // A labelled box of controls, laid out side by side.
    struct Group
    {
        std::unique_ptr<juce::GroupComponent> box;
        std::vector<Control*> controls;
        int row;
    };

    juce::AudioProcessorValueTreeState& parameters;
    PresetManager& presets;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<Group> groups;

    juce::Label presetLabel;
    PresetMenu presetMenu;
    juce::TextButton saveButton { "Save..." };
    juce::StringArray userPresetNames; // as the menu lists them

    // Greys out LFO Rate while LFO Sync is on, and LFO Sync Rate while it's off: only one of them
    // sets the LFO's speed at a time.
    std::unique_ptr<juce::ParameterAttachment> syncAttachment;

    Control& addControl (const char* parameterId, const char* labelText);
    static int getCellWidth (const Control& control);
    static int getGroupWidth (const Group& group);
    juce::Component* findControl (const char* parameterId) const;
    void showWhichRateApplies (bool synced);

    void refreshPresetMenu();
    void presetChosen();
    void askToSavePreset();
    void savePreset (const juce::String& name);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoringSynthEditor)
};

} // namespace boringsynth
