#pragma once

#include "Oscillator.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace boringsynth
{

// JUCE's Synthesiser needs at least one "sound". This one covers every note on every channel.
struct SynthSound  : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// The plug-in's parameters a voice reads, as the processor's live, thread-safe values. A new
// parameter that voices need goes here, and where the processor fills this in.
struct VoiceParameters
{
    const std::atomic<float>& velocitySensitivity; // 0 to 100 %, read when each note starts
    const std::atomic<float>& pitchBendRange;      // semitones, read on every block
};

// One playing note: an oscillator at the note's pitch, scaled by velocity, bent by the pitch wheel,
// and faded out over a few milliseconds when the note ends.
class SynthVoice  : public juce::SynthesiserVoice
{
public:
    // Length of the fade-out on note off. Short enough to sound instant, long enough not to click.
    static constexpr double releaseSeconds = 0.005;

    // waveTable is the waveform the voice plays. The table and the parameter values must outlive
    // the voice; the struct itself is copied.
    SynthVoice (const WaveTable& waveTable, const VoiceParameters& voiceParameters);

    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;

private:
    VoiceParameters parameters;
    Oscillator oscillator;
    double phaseDelta = 0.0;   // per sample, for the note's unbent pitch; 0 when the voice is idle
    float noteLevel = 0.0f;    // fixed per note: the base level scaled by velocity
    float gain = 0.0f;         // the release fade, from 1 down to 0
    float gainStep = 0.0f;
    float bendPosition = 0.0f; // the pitch wheel, from -1 (fully down) to 1 (fully up)

    // Multiplies the note's frequency. Smoothed multiplicatively, so the glide is even in pitch.
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> frequencyRatio { 1.0 };

    double getBendRatio() const;
};

} // namespace boringsynth
