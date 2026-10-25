#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace boringsynth
{

// Parameter and state identifiers are saved in DAW projects. Changing one breaks every project
// that uses it, so treat them as permanent.
constexpr const char* volumeId = "volume";
constexpr const char* velocitySensitivityId = "velocitySensitivity";
constexpr const char* pitchBendRangeId = "pitchBendRange";
constexpr const char* waveformId = "waveform";

// The amp (volume) envelope. "amp" because a filter envelope with its own attack, decay, sustain
// and release comes later.
constexpr const char* ampAttackId = "ampAttack";
constexpr const char* ampDecayId = "ampDecay";
constexpr const char* ampSustainId = "ampSustain";
constexpr const char* ampReleaseId = "ampRelease";

constexpr const char* filterEnabledId = "filterEnabled";
constexpr const char* filterCutoffId = "filterCutoff";
constexpr const char* filterResonanceId = "filterResonance";
constexpr const char* filterTypeId = "filterType";

// The kinds of filter, in the order the Filter Type parameter lists them. A DAW saves the choice as
// its position in this list, so the order is permanent: new types go at the end.
inline const juce::StringArray filterTypeNames { "Low-pass", "High-pass", "Band-pass" };
constexpr int lowPassType = 0, highPassType = 1, bandPassType = 2;

constexpr const char* stateTag = "Parameters";
constexpr const char* stateVersionId = "stateVersion";

// Bump this when the saved state changes in a way older versions need converting from.
constexpr int currentStateVersion = 1;

// Volume range in decibels. The minimum means silence.
constexpr float minVolumeDb = -60.0f;
constexpr float maxVolumeDb = 6.0f;

// Pitch bend range in semitones, either way. Two is the common default for synths.
constexpr int maxPitchBendRange = 24;
constexpr int defaultPitchBendRange = 2;

// Envelope times in seconds. Decay and release never go below 5 ms, so a note can never jump in
// level and click; an attack of 0 is fine, because every waveform starts at zero.
constexpr float maxEnvelopeSeconds = 10.0f;
constexpr float minDecaySeconds = 0.005f;
constexpr float minReleaseSeconds = 0.005f;

// The time knobs are skewed so the middle of their travel is half a second, giving most of the
// knob to short times, where small changes matter most.
constexpr float envelopeKnobCentreSeconds = 0.5f;

// Filter cutoff in Hz, over the range of hearing. The knob is skewed so equal turns change the
// cutoff by equal musical intervals: its middle is the geometric mean of the range, about 632 Hz.
constexpr float minCutoffHz = 20.0f;
constexpr float maxCutoffHz = 20000.0f;
constexpr float cutoffKnobCentreHz = 632.46f;

// Every parameter the plug-in has, with its range, default and display text.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace boringsynth
