#include "Parameters.h"

namespace boringsynth
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    auto volumeToText = [] (float value, int)
    {
        return value <= minVolumeDb ? juce::String ("-inf") : juce::String (value, 1);
    };

    auto textToVolume = [] (const juce::String& text)
    {
        return text.trim().startsWithIgnoreCase ("-inf") ? minVolumeDb : text.getFloatValue();
    };

    auto percentToText = [] (float value, int) { return juce::String (juce::roundToInt (value)); };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { volumeId, 1 },
                                                             "Volume",
                                                             juce::NormalisableRange<float> (minVolumeDb, maxVolumeDb, 0.1f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withLabel ("dB")
                                                                 .withStringFromValueFunction (volumeToText)
                                                                 .withValueFromStringFunction (textToVolume)));

    // Defaults to 0 %, which ignores velocity: the plug-in's behaviour before this existed.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { velocitySensitivityId, 1 },
                                                             "Velocity Sensitivity",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withLabel ("%")
                                                                 .withStringFromValueFunction (percentToText)));

    // 0 ignores the pitch wheel.
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { pitchBendRangeId, 1 },
                                                           "Pitch Bend Range",
                                                           0,
                                                           maxPitchBendRange,
                                                           defaultPitchBendRange,
                                                           juce::AudioParameterIntAttributes().withLabel ("semitones")));

    return layout;
}
} // namespace boringsynth
