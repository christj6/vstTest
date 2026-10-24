#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace boringsynth
{
// JUCE's Synthesiser handles the sustain and sostenuto pedals by default (and ignores the soft
// pedal). This plug-in keeps the sustain pedal, which nearly every keyboard has, and ignores
// sostenuto, as most synths do.
class Synth  : public juce::Synthesiser
{
public:
    void handleSostenutoPedal (int, bool) override {}
};
} // namespace boringsynth
