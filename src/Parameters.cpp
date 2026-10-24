#include "Parameters.h"

#include "Waveforms.h"

namespace boringsynth
{

namespace
{

// Times under a second show in milliseconds ("5 ms"), longer ones in seconds ("1.50 s").
juce::String secondsToText (float seconds, int)
{
    return seconds < 1.0f ? juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms"
                          : juce::String (seconds, 2) + " s";
}

// Accepts "250 ms", "0.25 s" or a plain number of seconds.
float textToSeconds (const juce::String& text)
{
    const auto trimmed = text.trim();
    return trimmed.endsWithIgnoreCase ("ms") ? trimmed.getFloatValue() / 1000.0f : trimmed.getFloatValue();
}

std::unique_ptr<juce::AudioParameterFloat> makeTimeParameter (const char* id, const char* name,
                                                              float minimumSeconds, float defaultSeconds)
{
    juce::NormalisableRange<float> range (minimumSeconds, maxEnvelopeSeconds);
    range.setSkewForCentre (envelopeKnobCentreSeconds);

    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 },
                                                        name,
                                                        range,
                                                        defaultSeconds,
                                                        juce::AudioParameterFloatAttributes()
                                                            .withStringFromValueFunction (secondsToText)
                                                            .withValueFromStringFunction (textToSeconds));
}

} // namespace

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

    // Sine, the plug-in's only waveform before this existed. The choices are saved by position, so
    // waveformNames' order is permanent.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { waveformId, 1 },
                                                              "Waveform",
                                                              waveformNames,
                                                              0));

    // The defaults reproduce the plug-in before the envelope existed: full level at once, held
    // while the key is down, and a 5 ms fade on release. Decay only matters once Sustain is lowered.
    layout.add (makeTimeParameter (ampAttackId, "Amp Attack", 0.0f, 0.0f));
    layout.add (makeTimeParameter (ampDecayId, "Amp Decay", minDecaySeconds, 0.2f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ampSustainId, 1 },
                                                             "Amp Sustain",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f),
                                                             100.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withLabel ("%")
                                                                 .withStringFromValueFunction (percentToText)));
    layout.add (makeTimeParameter (ampReleaseId, "Amp Release", minReleaseSeconds, minReleaseSeconds));

    return layout;
}

} // namespace boringsynth
