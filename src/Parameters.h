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

// The amp (volume) envelope. "amp" to tell it apart from the filter envelope below.
constexpr const char* ampAttackId = "ampAttack";
constexpr const char* ampDecayId = "ampDecay";
constexpr const char* ampSustainId = "ampSustain";
constexpr const char* ampReleaseId = "ampRelease";

constexpr const char* filterEnabledId = "filterEnabled";
constexpr const char* filterCutoffId = "filterCutoff";
constexpr const char* filterResonanceId = "filterResonance";
constexpr const char* filterTypeId = "filterType";
constexpr const char* filterKeyTrackingId = "filterKeyTracking";

// The filter envelope, which moves each note's cutoff over time.
constexpr const char* filterAttackId = "filterAttack";
constexpr const char* filterDecayId = "filterDecay";
constexpr const char* filterSustainId = "filterSustain";
constexpr const char* filterReleaseId = "filterRelease";
constexpr const char* filterEnvAmountId = "filterEnvAmount";

// How much softer notes close the filter.
constexpr const char* filterVelocityId = "filterVelocity";

// The LFO, which every note shares. Its depth is a percentage, not a pitch, because it can move the
// volume or the filter cutoff instead.
constexpr const char* lfoRateId = "lfoRate";
constexpr const char* lfoDepthId = "lfoDepth";
constexpr const char* lfoDestinationId = "lfoDestination";
constexpr const char* lfoShapeId = "lfoShape";

// What the LFO can move, in the order the LFO Destination parameter lists them. A DAW saves the
// choice as its position in this list, so the order is permanent: new destinations go at the end.
inline const juce::StringArray lfoDestinationNames { "Pitch", "Volume", "Filter Cutoff" };
constexpr int pitchDestination = 0, volumeDestination = 1, cutoffDestination = 2;

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

// How far the filter envelope can move the cutoff, in octaves either way, at its peak. Eight octaves
// covers most of the range of hearing, which is about ten.
constexpr float maxFilterEnvOctaves = 8.0f;

// How far velocity can lower the cutoff of the softest notes, in octaves. The same reach as the
// filter envelope.
constexpr float maxFilterVelocityOctaves = 8.0f;

// LFO rate in Hz, from a slow sweep to a fast wobble. The knob is skewed so equal turns are equal
// steps in speed: its middle is the geometric mean of the range, 1 Hz. 5 Hz is a typical vibrato.
constexpr float minLfoRateHz = 0.05f;
constexpr float maxLfoRateHz = 20.0f;
constexpr float lfoRateKnobCentreHz = 1.0f;
constexpr float defaultLfoRateHz = 5.0f;

// How far the LFO at full depth moves the pitch, in semitones either way.
constexpr float maxLfoPitchSemitones = 12.0f;

// How far the LFO at full depth moves the filter cutoff, in octaves either way: an eight-octave
// sweep, for the big "wobble" this is used for.
constexpr float maxLfoCutoffOctaves = 4.0f;

// Every parameter the plug-in has, with its range, default and display text.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace boringsynth
