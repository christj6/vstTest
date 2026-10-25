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

    // Each takes a sample position in the output buffer. Unless the LFO is sent to that
    // destination, the pitch ratio and gain are exactly 1 and the cutoff offset exactly 0.

    // Multiplies a note's pitch.
    float getPitchRatio (int sample) const noexcept { return pitchRatios[index (sample)]; }

    // Multiplies a note's level: never more than 1.
    float getGain (int sample) const noexcept { return gains[index (sample)]; }

    // Moves a note's filter cutoff, in octaves.
    float getCutoffOctaves (int sample) const noexcept { return cutoffOctaves[index (sample)]; }

    // Whether the LFO is moving the cutoff at all, even where it happens to be at 0 octaves.
    bool movesCutoff (int sample) const noexcept { return cutoffMoving[index (sample)]; }

private:
    friend class Synth;

    size_t index (int sample) const noexcept { return (size_t) (sample - startSample); }

    std::array<float, maxSamples> pitchRatios {};
    std::array<float, maxSamples> gains {};
    std::array<float, maxSamples> cutoffOctaves {};
    std::array<bool, maxSamples> cutoffMoving {};
    int startSample = 0; // the output buffer position the arrays' first entries are for
};

// The plug-in's parameters the LFO reads, as the processor's live, thread-safe values.
struct LfoParameters
{
    const std::atomic<float>& rate;        // Hz
    const std::atomic<float>& depth;       // 0 to 100 %
    const std::atomic<float>& destination; // position in lfoDestinationNames
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

    // How much of the LFO each destination gets (indexed by position in lfoDestinationNames): 1 for
    // the chosen one and 0 for the others, except while crossfading after the destination changes.
    std::array<juce::SmoothedValue<float>, 3> destinationAmounts;

    Modulation modulation;

    int getCurrentDestination() const;
};

} // namespace boringsynth
