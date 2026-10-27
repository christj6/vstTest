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

// The preset bar along the top.
constexpr int presetBarHeight = 28;
constexpr int presetLabelWidth = 52;
constexpr int presetMenuWidth = 260;
constexpr int saveButtonWidth = 80;

// Preset menu item IDs: the factory presets from 1, yours from 1001.
constexpr int firstFactoryItemId = 1;
constexpr int firstUserItemId = 1001;
constexpr int noUserPresetsItemId = 1000;

} // namespace

BoringSynthEditor::BoringSynthEditor (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state,
                                      PresetManager& presetManager, std::atomic<float>& windowScale)
    : AudioProcessorEditor (processor),
      parameters (state),
      presets (presetManager),
      scale (windowScale)
{
    presetLabel.setText ("Preset", juce::dontSendNotification);
    addAndMakeVisible (presetLabel);

    presetMenu.setComponentID ("presetMenu");
    presetMenu.setTitle ("Preset");
    presetMenu.beforeShowing = [this] { refreshPresetMenu(); };
    presetMenu.onChange = [this] { presetChosen(); };
    addAndMakeVisible (presetMenu);

    saveButton.setComponentID ("savePreset");
    saveButton.onClick = [this] { askToSavePreset(); };
    addAndMakeVisible (saveButton);

    refreshPresetMenu();
    presets.addChangeListener (this);

    for (const auto& layout : getLayout())
    {
        Group group { std::make_unique<juce::GroupComponent> (juce::String(), layout.title), {}, layout.row };
        addAndMakeVisible (*group.box);

        for (const auto& [parameterId, labelText] : layout.controls)
            group.controls.push_back (&addControl (parameterId, labelText));

        groups.push_back (std::move (group));
    }

    for (int row = 0; row < numRows; ++row)
        normalWidth = std::max (normalWidth, getRowWidth (row));

    normalWidth += gap;
    normalHeight = gap + presetBarHeight + gap + numRows * (groupHeight + gap);

    if (auto* sync = parameters.getParameter (lfoSyncId))
    {
        syncAttachment = std::make_unique<juce::ParameterAttachment> (*sync, [this] (float value) { showWhichRateApplies (value >= 0.5f); });
        syncAttachment->sendInitialUpdate();
    }

    // Read before setting the limits, which resize the still-empty window to the smallest size.
    const auto openingScale = scale.load();

    setResizable (true, true);
    setResizeLimits (juce::roundToInt ((float) normalWidth * minScale), juce::roundToInt ((float) normalHeight * minScale),
                     juce::roundToInt ((float) normalWidth * maxScale), juce::roundToInt ((float) normalHeight * maxScale));
    getConstrainer()->setFixedAspectRatio ((double) normalWidth / (double) normalHeight);
    setSize (juce::roundToInt ((float) normalWidth * openingScale), juce::roundToInt ((float) normalHeight * openingScale));
}

BoringSynthEditor::~BoringSynthEditor()
{
    presets.removeChangeListener (this);
}

//==============================================================================
// Presets.

// Lists the factory presets, then yours, and shows the current one.
void BoringSynthEditor::refreshPresetMenu()
{
    presetMenu.clear (juce::dontSendNotification);
    presetMenu.addSectionHeading ("Factory");

    for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
        presetMenu.addItem (presets.getFactoryPresetName (i), firstFactoryItemId + i);

    presetMenu.addSectionHeading ("Your presets");
    userPresetNames = presets.getUserPresetNames();

    if (userPresetNames.isEmpty())
    {
        presetMenu.addItem ("(none saved yet)", noUserPresetsItemId);
        presetMenu.setItemEnabled (noUserPresetsItemId, false);
    }

    for (int i = 0; i < userPresetNames.size(); ++i)
        presetMenu.addItem (userPresetNames[i], firstUserItemId + i);

    const auto current = presets.getCurrentPresetName();

    if (presets.getCurrentFactoryPreset() >= 0)
        presetMenu.setSelectedId (firstFactoryItemId + presets.getCurrentFactoryPreset(), juce::dontSendNotification);
    else if (userPresetNames.contains (current))
        presetMenu.setSelectedId (firstUserItemId + userPresetNames.indexOf (current), juce::dontSendNotification);
    else
        presetMenu.setText (current, juce::dontSendNotification);
}

void BoringSynthEditor::presetChosen()
{
    const auto id = presetMenu.getSelectedId();

    if (id >= firstUserItemId)
        presets.loadUserPreset (userPresetNames[id - firstUserItemId]);
    else if (id >= firstFactoryItemId && id < noUserPresetsItemId)
        presets.loadFactoryPreset (id - firstFactoryItemId);
}

// Asks for a name (offering the current preset's, if it's one of yours), then saves.
void BoringSynthEditor::askToSavePreset()
{
    auto* window = new juce::AlertWindow ("Save preset", "Save the current settings as a preset named:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", presets.getCurrentFactoryPreset() < 0 ? presets.getCurrentPresetName() : juce::String());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<BoringSynthEditor> editor (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([editor, window] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();

        if (editor == nullptr || result != 1 || name.isEmpty())
            return;

        if (! editor->presets.userPresetExists (name))
        {
            editor->savePreset (name);
            return;
        }

        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace preset?",
                                            "You already have a preset named \"" + name + "\". Replace it?",
                                            "Replace", "Cancel", editor,
                                            juce::ModalCallbackFunction::create ([editor, name] (int replace)
                                            {
                                                if (editor != nullptr && replace == 1)
                                                    editor->savePreset (name);
                                            }));
    }), true);
}

void BoringSynthEditor::savePreset (const juce::String& name)
{
    if (! presets.saveUserPreset (name))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save the preset",
                                                "BoringSynth couldn't write to " + presets.getUserPresetFolder().getFullPathName() + ".",
                                                {}, this);
}

void BoringSynthEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshPresetMenu();
}

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
    // Everything is laid out at the normal size, then drawn scaled up or down to fill the window.
    const auto windowScale = (float) getWidth() / (float) normalWidth;
    const auto transform = juce::AffineTransform::scale (windowScale);
    scale = windowScale;

    auto place = [&transform] (juce::Component& component, juce::Rectangle<int> bounds)
    {
        component.setTransform (transform);
        component.setBounds (bounds);
    };

    auto presetBar = juce::Rectangle<int> (gap, gap, normalWidth - 2 * gap, presetBarHeight);
    place (presetLabel, presetBar.removeFromLeft (presetLabelWidth));
    place (presetMenu, presetBar.removeFromLeft (presetMenuWidth));
    presetBar.removeFromLeft (gap);
    place (saveButton, presetBar.removeFromLeft (saveButtonWidth));

    // Rows narrower than the widest share out the spare width among their cells, so every row ends at
    // the same place. Each control keeps its own width, centred in its cell.
    std::array<int, numRows> rowX;
    std::array<int, numRows> cellsPlaced {};
    rowX.fill (gap);

    for (auto& group : groups)
    {
        const auto row = (size_t) group.row;
        const auto x = rowX[row];
        const auto y = gap + presetBarHeight + gap + group.row * (groupHeight + gap);
        const auto spareWidth = normalWidth - gap - getRowWidth (group.row);
        const auto numCells = getNumCellsInRow (group.row);
        auto cellX = x + groupPadding;

        for (auto* control : group.controls)
        {
            const auto extraWidth = spareWidth / numCells + (cellsPlaced[row]++ < spareWidth % numCells ? 1 : 0);
            const auto cell = juce::Rectangle<int> (cellX, y + groupTitleHeight, getCellWidth (*control) + extraWidth, cellHeight);
            cellX += cell.getWidth();
            auto area = cell.withSizeKeepingCentre (getCellWidth (*control), cellHeight).reduced (2, 0);

            if (control->buttonAttachment != nullptr)
            {
                place (*control->component, area.withSizeKeepingCentre (area.getWidth(), menuHeight));
            }
            else
            {
                place (control->label, area.removeFromTop (labelHeight));

                if (control->comboBoxAttachment != nullptr)
                    place (*control->component, area.withSizeKeepingCentre (area.getWidth(), menuHeight));
                else
                    place (*control->component, area);
            }
        }

        const auto width = cellX + groupPadding - x;
        place (*group.box, { x, y, width, groupHeight });
        rowX[row] += width + gap;
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

// A row's width at its natural size: its groups, each followed by a gap.
int BoringSynthEditor::getRowWidth (int row) const
{
    int width = 0;

    for (const auto& group : groups)
        if (group.row == row)
            width += getGroupWidth (group) + gap;

    return width;
}

int BoringSynthEditor::getNumCellsInRow (int row) const
{
    int numCells = 0;

    for (const auto& group : groups)
        if (group.row == row)
            numCells += (int) group.controls.size();

    return numCells;
}

} // namespace boringsynth
