#pragma once

#include "Lfo.h"
#include "Parameters.h"
#include "Waveforms.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <optional>

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
    const std::atomic<float>& rate;          // Hz
    const std::atomic<float>& depth;         // 0 to 100 %
    const std::atomic<float>& destination;   // position in lfoDestinationNames
    const std::atomic<float>& shape;         // position in waveformNames
    const std::atomic<float>& modWheelDepth; // 0 to 100 %, added to depth with the wheel fully up
    const std::atomic<float>& retrigger;     // 0 (free-running) or 1 (restarted by every note)
    const std::atomic<float>& sync;          // 0 (rate in Hz) or 1 (rate as a note length at the tempo)
    const std::atomic<float>& syncRate;      // position in lfoSyncRateNames
};

// What the host says about its transport at the start of a block. Hosts may leave out the tempo or
// the song position.
struct Transport
{
    std::optional<double> tempoBpm;
    std::optional<double> songPositionQuarterNotes; // the song position at the block's first sample
    bool isPlaying = false;
};

// JUCE's Synthesiser, plus the LFO that every voice shares, and the mod wheel that adds to its depth.
//
// JUCE's Synthesiser handles the sustain and sostenuto pedals by default (and ignores the soft
// pedal). This plug-in keeps the sustain pedal, which nearly every keyboard has, and ignores
// sostenuto, as most synths do.
class Synth  : public juce::Synthesiser
{
public:
    // The parameter values must outlive the synth.
    explicit Synth (const LfoParameters& lfoParameters);

    // The LFO's effect on the voices, for the samples they're rendering.
    const Modulation& getModulation() const noexcept { return modulation; }

    void setCurrentPlaybackSampleRate (double newRate) override;
    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override;
    void handleController (int midiChannel, int controllerNumber, int controllerValue) override;
    void handleSostenutoPedal (int, bool) override {}

    // Called at the start of every block, before it's rendered, with what the host says about its
    // transport: a synced LFO follows its tempo and, while it plays, its song position.
    void setTransport (const Transport& transport);

    // How long the saw's and square's jumps take, as ramps, in seconds: as long as the shortest
    // release, so they don't click. Also how long the LFO takes to fade when it jumps elsewhere in its
    // cycle (when a note retriggers it, or the song it's locked to jumps).
    static constexpr double lfoJumpSeconds = 0.005;

protected:
    using juce::Synthesiser::renderVoices;
    void renderVoices (juce::AudioBuffer<float>& outputAudio, int startSample, int numSamples) override;

private:
    LfoParameters parameters;

    // One LFO for every note, running freely whether or not notes are playing, like a hardware
    // synth's.
    Lfo lfo;
    juce::SmoothedValue<float> lfoDepth; // 0 to 1, the knob plus the mod wheel

    // The mod wheel's position, from 0 (down) to 1 (fully up), from whichever MIDI channel moved it
    // last. Only touched on the audio thread, where MIDI is handled.
    float modWheel = 0.0f;

    // The host's tempo, or the last one it gave, or defaultTempoBpm if it never has.
    double tempoBpm = defaultTempoBpm;

    // How much of the LFO each destination gets (indexed by position in lfoDestinationNames): 1 for
    // the chosen one and 0 for the others, except while crossfading after the destination changes.
    std::array<juce::SmoothedValue<float>, 3> destinationAmounts;

    Modulation modulation;

    float getLfoDepthTarget() const;
    double getLfoRateHz() const;
    double getSyncedCycleQuarterNotes() const;
    bool isSynced() const;
    int getCurrentShape() const;
    int getCurrentDestination() const;
};

} // namespace boringsynth
