#pragma once

#include "PresetManager.h"
#include "WavetableDisplay.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace boringsynth
{

// The plug-in's window: a preset bar along the top (a menu of the factory presets and yours, and a
// Save button), then every parameter as one of JUCE's standard controls, in labelled groups (Global,
// Oscillator, Amp Envelope, Position Envelope, Filter Envelope, Filter, LFO). Knobs for numbers, showing the parameter's
// own value text; menus for choices; tick boxes for switches. Each control is connected to its
// parameter by a JUCE parameter attachment, which keeps the two in step both ways, including when
// the host automates the parameter. JUCE's own look: it's BoringSynth. The one thing it draws itself is
// a small picture of the waveform, under the Waveform menu (WavetableDisplay).
//
// The window can be resized from its corner, keeping its shape: everything is drawn bigger or
// smaller, from 75 % to 200 %, rather than laid out differently. The project remembers the size.
//
// Everything is in one component, laid out once at the normal size, which resizing the window scales.
//
// Each control's component ID is its parameter's ID, so tests can find them; the preset menu's is
// "presetMenu", the Save button's "savePreset", the picture's "wavetableDisplay", the Load wavetable
// button's "loadWavetable", and the text beside it "wavetableStatus".
class BoringSynthEditor  : public juce::AudioProcessorEditor,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    // How big the window can be drawn, as a share of its normal size.
    static constexpr float minScale = 0.75f;
    static constexpr float maxScale = 2.0f;

    // parameters, presets, scale, tables, importer and playedPosition belong to the processor, or outlive
    // it; all must outlive the editor, as JUCE ensures for a processor's editor. The window opens at scale,
    // and keeps it up to date when resized. See WavetableDisplay for the last three.
    BoringSynthEditor (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& parameters,
                       PresetManager& presets, std::atomic<float>& scale, const WaveformTables& tables,
                       WavetableImporter& importer, const std::atomic<float>& playedPosition);
    ~BoringSynthEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Loads a wavetable, as choosing a file with the Load wavetable button does, and switches the
    // waveform to Imported once it's loaded.
    void loadWavetable (const juce::File& file);

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
    std::atomic<float>& scale;

    // Everything in the window, laid out once at the normal size, and drawn scaled to fill it. Declared
    // before the components in it, so it goes after them.
    juce::Component content;

    std::vector<std::unique_ptr<Control>> controls;
    std::vector<Group> groups;
    WavetableDisplay wavetableDisplay;

    juce::Label presetLabel;
    PresetMenu presetMenu;
    juce::TextButton saveButton { "Save..." };
    juce::StringArray userPresetNames; // as the menu lists them

    WavetableImporter& wavetableImporter;
    juce::TextButton loadWavetableButton { "Load wavetable..." };
    juce::Label wavetableStatus;
    std::unique_ptr<juce::FileChooser> wavetableChooser;
    int generationWhenLoading = 0; // the importer's, to tell whether a load has worked

    // The window's size at 100 %. Everything is laid out at this size, then drawn scaled to the window.
    int normalWidth = 0;
    int normalHeight = 0;

    // Greys out LFO Rate while LFO Sync is on, and LFO Sync Rate while it's off: only one of them
    // sets the LFO's speed at a time.
    std::unique_ptr<juce::ParameterAttachment> syncAttachment;

    // Greys out Position and its envelope unless the waveform is a wavetable: they do nothing to the others.
    std::unique_ptr<juce::ParameterAttachment> waveformAttachment;

    Control& addControl (const char* parameterId, const char* labelText);
    void layOut();
    static int getCellWidth (const Control& control);
    static int getGroupWidth (const Group& group);
    int getRowWidth (int row) const;
    int getNumCellsInRow (int row) const;
    juce::Component* findControl (const char* parameterId) const;
    void showWhichRateApplies (bool synced);

    void refreshPresetMenu();
    void presetChosen();
    void askToSavePreset();
    void savePreset (const juce::String& name);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void chooseWavetable();
    void showWavetableStatus();
    void timerCallback() override; // while a wavetable loads

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoringSynthEditor)
};

} // namespace boringsynth
