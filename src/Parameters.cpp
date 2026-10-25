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

// Frequencies under 1 kHz show in hertz ("440 Hz"), higher ones in kilohertz ("1.20 kHz").
juce::String hertzToText (float hertz, int)
{
    return hertz < 1000.0f ? juce::String (juce::roundToInt (hertz)) + " Hz"
                           : juce::String (hertz / 1000.0f, 2) + " kHz";
}

// Accepts "440 Hz", "1.2 kHz", "1.2k" or a plain number of hertz.
float textToHertz (const juce::String& text)
{
    const auto trimmed = text.trim();
    const auto isKilohertz = trimmed.endsWithIgnoreCase ("khz") || trimmed.endsWithIgnoreCase ("k");
    return isKilohertz ? trimmed.getFloatValue() * 1000.0f : trimmed.getFloatValue();
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
                                                              sineWaveform));

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

    // Off by default, so the plug-in sounds exactly as it did before the filter existed. When it's
    // switched on, the cutoff starts fully open and the resonance flat, so nothing changes until
    // they're moved.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { filterEnabledId, 1 }, "Filter", false));

    juce::NormalisableRange<float> cutoffRange (minCutoffHz, maxCutoffHz);
    cutoffRange.setSkewForCentre (cutoffKnobCentreHz);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { filterCutoffId, 1 },
                                                             "Filter Cutoff",
                                                             cutoffRange,
                                                             maxCutoffHz,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (hertzToText)
                                                                 .withValueFromStringFunction (textToHertz)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { filterResonanceId, 1 },
                                                             "Filter Resonance",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withLabel ("%")
                                                                 .withStringFromValueFunction (percentToText)));

    // Low-pass, the only type before this existed. The choices are saved by position, so
    // filterTypeNames' order is permanent.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { filterTypeId, 1 },
                                                              "Filter Type",
                                                              filterTypeNames,
                                                              lowPassType));

    // How far the cutoff follows the note played: 0 % keeps it where the knob says for every note
    // (the behaviour before this existed); 100 % moves it an octave for every octave played.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { filterKeyTrackingId, 1 },
                                                             "Filter Key Tracking",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withLabel ("%")
                                                                 .withStringFromValueFunction (percentToText)));

    // The filter envelope's shape defaults to a classic "pluck": bright at the start of each note,
    // closing over 300 ms. It only matters once the amount is turned up, which defaults to 0: no
    // effect, the behaviour before this existed.
    layout.add (makeTimeParameter (filterAttackId, "Filter Attack", 0.0f, 0.0f));
    layout.add (makeTimeParameter (filterDecayId, "Filter Decay", minDecaySeconds, 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { filterSustainId, 1 },
                                                             "Filter Sustain",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withLabel ("%")
                                                                 .withStringFromValueFunction (percentToText)));
    layout.add (makeTimeParameter (filterReleaseId, "Filter Release", minReleaseSeconds, 0.3f));

    // In octaves: how far the cutoff moves when the envelope is at its peak. Negative amounts move it
    // down instead.
    auto octavesToText = [] (float octaves, int)
    {
        return (octaves > 0.0f ? "+" : "") + juce::String (octaves, 2) + " oct";
    };

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { filterEnvAmountId, 1 },
                                                             "Filter Env Amount",
                                                             juce::NormalisableRange<float> (-maxFilterEnvOctaves, maxFilterEnvOctaves, 0.01f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (octavesToText)));

    // In octaves: how far the cutoff of the softest notes is lowered. The hardest notes always get
    // the cutoff the knob sets, as with Velocity Sensitivity and level. Defaults to 0, which ignores
    // velocity: the behaviour before this existed.
    auto velocityOctavesToText = [] (float octaves, int) { return juce::String (octaves, 2) + " oct"; };

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { filterVelocityId, 1 },
                                                             "Filter Velocity",
                                                             juce::NormalisableRange<float> (0.0f, maxFilterVelocityOctaves, 0.01f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (velocityOctavesToText)));

    juce::NormalisableRange<float> lfoRateRange (minLfoRateHz, maxLfoRateHz);
    lfoRateRange.setSkewForCentre (lfoRateKnobCentreHz);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { lfoRateId, 1 },
                                                             "LFO Rate",
                                                             lfoRateRange,
                                                             defaultLfoRateHz,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float hertz, int) { return juce::String (hertz, 2) + " Hz"; })
                                                                 .withValueFromStringFunction (textToHertz)));

    // Defaults to 0 %, no effect: the behaviour before the LFO existed.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { lfoDepthId, 1 },
                                                             "LFO Depth",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withLabel ("%")
                                                                 .withStringFromValueFunction (percentToText)));

    // Pitch, the LFO's only destination before this existed. The choices are saved by position, so
    // lfoDestinationNames' order is permanent.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { lfoDestinationId, 1 },
                                                              "LFO Destination",
                                                              lfoDestinationNames,
                                                              pitchDestination));

    // The same shapes as Waveform, in the same permanent order. Sine, the LFO's only shape before
    // this existed.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { lfoShapeId, 1 },
                                                              "LFO Shape",
                                                              waveformNames,
                                                              sineWaveform));

    // With the mod wheel fully up, this much is added to LFO Depth. Defaults to 0 %, which ignores the
    // wheel: the behaviour before this existed.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { modWheelLfoDepthId, 1 },
                                                             "Mod Wheel LFO Depth",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f),
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withLabel ("%")
                                                                 .withStringFromValueFunction (percentToText)));

    // Off by default: the LFO runs freely, as it did before this existed.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { lfoRetriggerId, 1 }, "LFO Retrigger", false));

    return layout;
}

} // namespace boringsynth
