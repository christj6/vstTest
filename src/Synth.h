#pragma once

#include "Oscillator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace boringsynth
{

// What the LFO is doing to every voice, worked out once per sample for all of them. Voices read it
// for the samples they're rendering.
class Modulation
{
public:
    // How many samples are worked out at a time. The synth renders its voices in pieces no longer
    // than this, so nothing needs allocating however large a block the host sends.
    static constexpr int maxSamples = 512;

    // Multiplies a note's pitch at a sample position in the output buffer. Exactly 1 when the
    // LFO's depth is 0.
    float getPitchRatio (int sample) const noexcept { return pitchRatios[(size_t) (sample - startSample)]; }

private:
    friend class Synth;

    std::array<float, maxSamples> pitchRatios {};
    int startSample = 0; // the output buffer position pitchRatios[0] is for
};

// The plug-in's parameters the LFO reads, as the processor's live, thread-safe values.
struct LfoParameters
{
    const std::atomic<float>& rate;  // Hz
    const std::atomic<float>& depth; // 0 to 100 %
};

// JUCE's Synthesiser, plus the LFO that every voice shares.
//
// JUCE's Synthesiser handles the sustain and sostenuto pedals by default (and ignores the soft
// pedal). This plug-in keeps the sustain pedal, which nearly every keyboard has, and ignores
// sostenuto, as most synths do.
class Synth  : public juce::Synthesiser
{
public:
    // lfoShape is the wave table the LFO reads. It and the parameter values must outlive the synth.
    Synth (const BandLimitedWaveTable& lfoShape, const LfoParameters& lfoParameters);

    // The LFO's effect on the voices, for the samples they're rendering.
    const Modulation& getModulation() const noexcept { return modulation; }

    void setCurrentPlaybackSampleRate (double newRate) override;
    void handleSostenutoPedal (int, bool) override {}

protected:
    using juce::Synthesiser::renderVoices;
    void renderVoices (juce::AudioBuffer<float>& outputAudio, int startSample, int numSamples) override;

private:
    LfoParameters parameters;

    // One LFO for every note, running freely whether or not notes are playing, like a hardware
    // synth's. Its shape is read from a wave table, like a note's.
    Oscillator lfo;
    juce::SmoothedValue<float> lfoDepth; // 0 to 1
    Modulation modulation;
};

} // namespace boringsynth
