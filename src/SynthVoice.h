#pragma once

#include "Envelope.h"
#include "Oscillator.h"
#include "Waveforms.h"

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
    const std::atomic<float>& waveform;            // position in waveformNames, read on every block

    // The amp envelope, read on every block.
    const std::atomic<float>& ampAttack;           // seconds
    const std::atomic<float>& ampDecay;            // seconds
    const std::atomic<float>& ampSustain;          // 0 to 100 %
    const std::atomic<float>& ampRelease;          // seconds
};

// One playing note: an oscillator at the note's pitch, scaled by velocity, bent by the pitch wheel,
// and shaped over time by the amp envelope.
class SynthVoice  : public juce::SynthesiserVoice
{
public:
    // waveformTables holds every waveform the voice can play. The tables and the parameter values
    // must outlive the voice; the struct itself is copied.
    SynthVoice (const WaveformTables& waveformTables, const VoiceParameters& voiceParameters);

    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;

private:
    const WaveformTables& tables;
    VoiceParameters parameters;
    Oscillator oscillator;
    double phaseDelta = 0.0;   // per sample, for the note's unbent pitch; 0 when the voice is idle
    float noteLevel = 0.0f;    // fixed per note: the base level scaled by velocity
    float bendPosition = 0.0f; // the pitch wheel, from -1 (fully down) to 1 (fully up)
    Envelope ampEnvelope;

    // Multiplies the note's frequency. Smoothed multiplicatively, so the glide is even in pitch.
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> frequencyRatio { 1.0 };

    double getBendRatio() const;
    const BandLimitedWaveTable& getCurrentTable() const;
    Envelope::Settings getAmpEnvelopeSettings() const;
};

} // namespace boringsynth
