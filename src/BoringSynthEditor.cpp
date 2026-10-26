#include "BoringSynthEditor.h"

#include "Parameters.h"

namespace boringsynth
{

namespace
{

// The groups, in the order they're laid out, row by row, each with its parameters and their labels.
struct GroupLayout
{
    const char* title;
    int row;
    std::vector<std::pair<const char*, const char*>> controls; // parameter ID, label
};

const std::vector<GroupLayout>& getLayout()
{
    static const std::vector<GroupLayout> layout {
        { "Global", 0, { { volumeId, "Volume" }, { velocitySensitivityId, "Velocity" }, { pitchBendRangeId, "Bend Range" } } },
        { "Oscillator", 0, { { waveformId, "Waveform" } } },
        { "Amp Envelope", 0, { { ampAttackId, "Attack" }, { ampDecayId, "Decay" }, { ampSustainId, "Sustain" }, { ampReleaseId, "Release" } } },
        { "Filter", 1, { { filterEnabledId, "On" }, { filterTypeId, "Type" }, { filterCutoffId, "Cutoff" },
                         { filterResonanceId, "Resonance" }, { filterKeyTrackingId, "Key Track" }, { filterVelocityId, "Velocity" } } },
        { "Filter Envelope", 1, { { filterAttackId, "Attack" }, { filterDecayId, "Decay" }, { filterSustainId, "Sustain" },
                                  { filterReleaseId, "Release" }, { filterEnvAmountId, "Amount" } } },
        { "LFO", 2, { { lfoRateId, "Rate" }, { lfoSyncId, "Sync" }, { lfoSyncRateId, "Sync Rate" }, { lfoDepthId, "Depth" },
                      { modWheelLfoDepthId, "Mod Wheel" }, { lfoDestinationId, "Destination" }, { lfoShapeId, "Shape" },
                      { lfoRetriggerId, "Retrigger" } } } };
    return layout;
}

// Sizes, in pixels. Every control takes one cell: its label, then the knob (with its value below),
// menu or tick box. Menus get wider cells, to fit their longest choice ("Filter Cutoff").
constexpr int cellWidth = 84;
constexpr int menuCellWidth = 116;
constexpr int cellHeight = 100;
constexpr int labelHeight = 18;
constexpr int valueBoxHeight = 18;
constexpr int menuHeight = 24;
constexpr int groupTitleHeight = 22;
constexpr int groupPadding = 8;
constexpr int gap = 10; // between groups, and around the edge
constexpr int numRows = 3;

constexpr int groupHeight = groupTitleHeight + cellHeight + groupPadding;

} // namespace

BoringSynthEditor::BoringSynthEditor (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state)
    : AudioProcessorEditor (processor),
      parameters (state)
{
    int width = 0;
    std::array<int, numRows> rowWidths {};

    for (const auto& layout : getLayout())
    {
        Group group { std::make_unique<juce::GroupComponent> (juce::String(), layout.title), {}, layout.row };
        addAndMakeVisible (*group.box);

        for (const auto& [parameterId, labelText] : layout.controls)
            group.controls.push_back (&addControl (parameterId, labelText));

        rowWidths[(size_t) layout.row] += getGroupWidth (group) + gap;
        groups.push_back (std::move (group));
    }

    for (auto rowWidth : rowWidths)
        width = std::max (width, rowWidth);

    if (auto* sync = parameters.getParameter (lfoSyncId))
    {
        syncAttachment = std::make_unique<juce::ParameterAttachment> (*sync, [this] (float value) { showWhichRateApplies (value >= 0.5f); });
        syncAttachment->sendInitialUpdate();
    }

    setSize (width + gap, numRows * (groupHeight + gap) + gap);
}

BoringSynthEditor::~BoringSynthEditor() = default;

BoringSynthEditor::Control& BoringSynthEditor::addControl (const char* parameterId, const char* labelText)
{
    auto& control = *controls.emplace_back (std::make_unique<Control>());
    auto* parameter = parameters.getParameter (parameterId);
    jassert (parameter != nullptr);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
    {
        auto comboBox = std::make_unique<juce::ComboBox>();
        comboBox->addItemList (choice->choices, 1);
        control.comboBoxAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*parameter, *comboBox);
        control.component = std::move (comboBox);
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr)
    {
        auto toggle = std::make_unique<juce::ToggleButton> (labelText);
        control.buttonAttachment = std::make_unique<juce::ButtonParameterAttachment> (*parameter, *toggle);
        control.component = std::move (toggle);
    }
    else
    {
        auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, cellWidth - 4, valueBoxHeight);
        control.sliderAttachment = std::make_unique<juce::SliderParameterAttachment> (*parameter, *slider);

        // The parameter's value text leaves out its unit when it has a separate label ("%", "dB").
        if (parameter->getLabel().isNotEmpty())
            slider->setTextValueSuffix (" " + parameter->getLabel());

        control.component = std::move (slider);
    }

    control.component->setComponentID (parameterId);
    control.component->setTitle (parameter->getName (100));
    addAndMakeVisible (*control.component);

    // A tick box shows its own text; knobs and menus get a label above them.
    if (control.buttonAttachment == nullptr)
    {
        control.label.setText (labelText, juce::dontSendNotification);
        control.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (control.label);
    }

    return control;
}

void BoringSynthEditor::showWhichRateApplies (bool synced)
{
    if (auto* rate = findControl (lfoRateId))
        rate->setEnabled (! synced);

    if (auto* syncRate = findControl (lfoSyncRateId))
        syncRate->setEnabled (synced);
}

juce::Component* BoringSynthEditor::findControl (const char* parameterId) const
{
    for (const auto& control : controls)
        if (control->component->getComponentID() == parameterId)
            return control->component.get();

    return nullptr;
}

void BoringSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void BoringSynthEditor::resized()
{
    std::array<int, numRows> rowX;
    rowX.fill (gap);

    for (auto& group : groups)
    {
        const auto x = rowX[(size_t) group.row];
        const auto y = gap + group.row * (groupHeight + gap);
        const auto width = getGroupWidth (group);
        group.box->setBounds (x, y, width, groupHeight);
        rowX[(size_t) group.row] += width + gap;

        auto cellX = x + groupPadding;

        for (auto* control : group.controls)
        {
            const auto cell = juce::Rectangle<int> (cellX, y + groupTitleHeight, getCellWidth (*control), cellHeight);
            cellX += cell.getWidth();
            auto area = cell.reduced (2, 0);

            if (control->buttonAttachment != nullptr)
            {
                control->component->setBounds (area.withSizeKeepingCentre (area.getWidth(), menuHeight));
            }
            else
            {
                control->label.setBounds (area.removeFromTop (labelHeight));

                if (control->comboBoxAttachment != nullptr)
                    control->component->setBounds (area.withSizeKeepingCentre (area.getWidth(), menuHeight));
                else
                    control->component->setBounds (area);
            }
        }
    }
}

int BoringSynthEditor::getCellWidth (const Control& control)
{
    return control.comboBoxAttachment != nullptr ? menuCellWidth : cellWidth;
}

int BoringSynthEditor::getGroupWidth (const Group& group)
{
    int width = 2 * groupPadding;

    for (auto* control : group.controls)
        width += getCellWidth (*control);

    return width;
}

} // namespace boringsynth
