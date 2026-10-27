#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace boringsynth
{

// Loads and saves presets: the built-in factory presets (FactoryPresets.h), and your own, saved as
// files. A preset file is the same XML as the plug-in's saved state (parameter IDs and values, with
// its state version), so it's readable, and a preset saved before a parameter existed loads with
// that parameter at its default.
//
// Also keeps the name of the current preset, which is saved with the project, and tells listeners
// (the editor) when it changes. Everything here is for the message thread, except
// loadFactoryPreset, which a host may call from elsewhere when it changes program: it touches no
// files, and listeners hear about it asynchronously.
class PresetManager  : public juce::ChangeBroadcaster
{
public:
    // The parameters must outlive this.
    PresetManager (juce::AudioProcessorValueTreeState& parameters, const juce::File& userPresetFolder);

    // Where your presets go: Documents/BoringSynth/Presets. Only created when you first save one.
    static juce::File getDefaultUserPresetFolder();
    void setUserPresetFolder (const juce::File& folder);
    const juce::File& getUserPresetFolder() const noexcept { return userPresetFolder; }

    int getNumFactoryPresets() const;
    juce::String getFactoryPresetName (int index) const;
    void loadFactoryPreset (int index);

    // Your presets' names, in alphabetical order, from the files in the folder.
    juce::StringArray getUserPresetNames() const;
    bool loadUserPreset (const juce::String& name);

    // Saves the current settings as a preset of this name, replacing any with the same name. A name
    // that can't be a file name (with "/" or ":", say) is made into one. Returns false if the name
    // is empty or the file couldn't be written.
    bool saveUserPreset (const juce::String& name);
    bool userPresetExists (const juce::String& name) const;

    // The current preset's name, and its position among the factory presets (or -1 if it isn't one).
    // A new plug-in starts on Init.
    juce::String getCurrentPresetName() const;
    int getCurrentFactoryPreset() const noexcept { return currentFactoryPreset; }

    // Saved with the project, so it reopens on the same preset.
    void writeTo (juce::ValueTree& state) const;
    void readFrom (const juce::ValueTree& state);

    // A preset as XML (the current settings), and setting every parameter from one. Applying fails,
    // changing nothing, if the XML isn't this plug-in's settings or is from a newer version of it.
    std::unique_ptr<juce::XmlElement> createPresetXml() const;
    bool applyPresetXml (const juce::XmlElement& xml);

private:
    juce::AudioProcessorValueTreeState& parameters;
    juce::File userPresetFolder;
    juce::String currentName;
    int currentFactoryPreset = 0;

    juce::File getUserPresetFile (const juce::String& name) const;
    void setCurrentPreset (const juce::String& name, int factoryIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace boringsynth
