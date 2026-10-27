#include "PresetManager.h"

#include "FactoryPresets.h"
#include "Parameters.h"

namespace boringsynth
{

namespace
{

// Saved-state properties for the current preset.
constexpr const char* presetNameId = "presetName";
constexpr const char* factoryPresetId = "factoryPreset";

constexpr const char* presetFileExtension = ".xml";

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

    // Every parameter the preset doesn't list goes back to its default.
    for (auto* parameter : parameters.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

        if (ranged == nullptr)
            continue;

        auto value = ranged->convertFrom0to1 (ranged->getDefaultValue());

        for (const auto& [id, presetValue] : preset.settings)
            if (ranged->getParameterID() == id)
                value = presetValue;

        setParameter (*ranged, value);
    }

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

    // Every parameter the preset doesn't hold (one saved before the parameter existed) goes back to
    // its default, as an older project's would.
    for (auto* parameter : parameters.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

        if (ranged == nullptr)
            continue;

        const auto* entry = xml.getChildByAttribute ("id", ranged->getParameterID());
        const auto value = entry != nullptr ? (float) entry->getDoubleAttribute ("value") : ranged->convertFrom0to1 (ranged->getDefaultValue());
        setParameter (*ranged, value);
    }

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

// Sets a parameter as the host would see a user moving it: one complete gesture, so hosts that
// record automation or undo treat it as one change.
void PresetManager::setParameter (juce::RangedAudioParameter& parameter, float value)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (value));
    parameter.endChangeGesture();
}

} // namespace boringsynth
