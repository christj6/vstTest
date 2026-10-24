#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace boringsynth
{

// Parameter and state identifiers are saved in DAW projects. Changing one breaks every project
// that uses it, so treat them as permanent.
constexpr const char* volumeId = "volume";
constexpr const char* velocitySensitivityId = "velocitySensitivity";
constexpr const char* pitchBendRangeId = "pitchBendRange";
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

// Every parameter the plug-in has, with its range, default and display text.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace boringsynth
