#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace boringsynth
{

// The plug-in's window: every parameter as one of JUCE's standard controls, in labelled groups (Global,
// Oscillator, Amp Envelope, Filter, Filter Envelope, LFO). Knobs for numbers, showing the parameter's
// own value text; menus for choices; tick boxes for switches. Each control is connected to its
// parameter by a JUCE parameter attachment, which keeps the two in step both ways, including when
// the host automates the parameter. A fixed size and JUCE's own look: it's BoringSynth.
//
// Each control's component ID is its parameter's ID, so tests can find them.
class BoringSynthEditor  : public juce::AudioProcessorEditor
{
public:
    // parameters holds the processor's parameters; both must outlive the editor, as JUCE ensures for
    // a processor's editor.
    BoringSynthEditor (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& parameters);
    ~BoringSynthEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
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
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<Group> groups;

    // Greys out LFO Rate while LFO Sync is on, and LFO Sync Rate while it's off: only one of them
    // sets the LFO's speed at a time.
    std::unique_ptr<juce::ParameterAttachment> syncAttachment;

    Control& addControl (const char* parameterId, const char* labelText);
    static int getCellWidth (const Control& control);
    static int getGroupWidth (const Group& group);
    juce::Component* findControl (const char* parameterId) const;
    void showWhichRateApplies (bool synced);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoringSynthEditor)
};

} // namespace boringsynth
