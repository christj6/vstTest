#include "PresetManager.h"

#include "FactoryPresets.h"
#include "Parameters.h"

#include <optional>

namespace boringsynth
{

namespace
{

// Saved-state properties for the current preset.
constexpr const char* presetNameId = "presetName";
constexpr const char* factoryPresetId = "factoryPreset";

constexpr const char* presetFileExtension = ".xml";

// Sets a parameter as the host would see a user moving it: one complete gesture, so hosts that
// record automation or undo treat it as one change.
void setParameter (juce::RangedAudioParameter& parameter, float value)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (value));
    parameter.endChangeGesture();
}

// Sets every parameter to the value valueFor gives it (an optional float), or, where it gives none,
// back to its default: a preset that doesn't mention a parameter leaves it at its default.
template <typename ValueFor>
void setEveryParameter (juce::AudioProcessor& processor, ValueFor valueFor)
{
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            setParameter (*ranged, valueFor (*ranged).value_or (ranged->convertFrom0to1 (ranged->getDefaultValue())));
}

} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, const juce::File& folder)
    : parameters (state),
      userPresetFolder (folder),
      currentName (getFactoryPresets().front().name)
{
}

juce::File PresetManager::getDefaultUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("BoringSynth").getChildFile ("Presets");
}

void PresetManager::setUserPresetFolder (const juce::File& folder)
{
    userPresetFolder = folder;
}

int PresetManager::getNumFactoryPresets() const
{
    return (int) getFactoryPresets().size();
}

juce::String PresetManager::getFactoryPresetName (int index) const
{
    return juce::isPositiveAndBelow (index, getNumFactoryPresets()) ? getFactoryPresets()[(size_t) index].name : "";
}

void PresetManager::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumFactoryPresets()))
        return;

    const auto& preset = getFactoryPresets()[(size_t) index];

    setEveryParameter (parameters.processor, [&preset] (const juce::RangedAudioParameter& parameter) -> std::optional<float>
    {
        for (const auto& [id, value] : preset.settings)
            if (parameter.getParameterID() == id)
                return value;

        return {};
    });

    setCurrentPreset (preset.name, index);
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;

    for (const auto& file : userPresetFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetFileExtension))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

bool PresetManager::loadUserPreset (const juce::String& name)
{
    const auto xml = juce::XmlDocument::parse (getUserPresetFile (name));

    if (xml == nullptr || ! applyPresetXml (*xml))
        return false;

    setCurrentPreset (getUserPresetFile (name).getFileNameWithoutExtension(), -1);
    return true;
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    if (juce::File::createLegalFileName (name.trim()).isEmpty())
        return false;

    const auto file = getUserPresetFile (name);
    const auto xml = createPresetXml();

    if (! userPresetFolder.createDirectory() || xml == nullptr || ! xml->writeTo (file))
        return false;

    setCurrentPreset (file.getFileNameWithoutExtension(), -1);
    return true;
}

bool PresetManager::userPresetExists (const juce::String& name) const
{
    return getUserPresetFile (name).existsAsFile();
}

juce::String PresetManager::getCurrentPresetName() const
{
    return currentName;
}

void PresetManager::writeTo (juce::ValueTree& state) const
{
    state.setProperty (presetNameId, currentName, nullptr);
    state.setProperty (factoryPresetId, currentFactoryPreset, nullptr);
}

void PresetManager::readFrom (const juce::ValueTree& state)
{
    // Projects saved before presets existed have neither: they show no preset name.
    setCurrentPreset (state.getProperty (presetNameId, "").toString(), (int) state.getProperty (factoryPresetId, -1));
}

std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml() const
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionId, currentStateVersion, nullptr);
    return state.createXml();
}

bool PresetManager::applyPresetXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (stateTag) || xml.getIntAttribute (stateVersionId, currentStateVersion) > currentStateVersion)
        return false;

    // A parameter the preset doesn't hold (one saved before the parameter existed) goes back to its
    // default, as an older project's would.
    setEveryParameter (parameters.processor, [&xml] (const juce::RangedAudioParameter& parameter) -> std::optional<float>
    {
        if (const auto* entry = xml.getChildByAttribute ("id", parameter.getParameterID()))
            return (float) entry->getDoubleAttribute ("value");

        return {};
    });

    return true;
}

juce::File PresetManager::getUserPresetFile (const juce::String& name) const
{
    return userPresetFolder.getChildFile (juce::File::createLegalFileName (name.trim()) + presetFileExtension);
}

void PresetManager::setCurrentPreset (const juce::String& name, int factoryIndex)
{
    currentName = name;
    currentFactoryPreset = factoryIndex;
    sendChangeMessage();
}

} // namespace boringsynth
