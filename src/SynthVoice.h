#pragma once

#include "Envelope.h"
#include "Filter.h"
#include "Oscillator.h"
#include "Parameters.h"
#include "Synth.h"
#include "Waveforms.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <optional>

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

    // The filter, read on every block.
    const std::atomic<float>& filterEnabled;       // 0 (off) or 1 (on)
    const std::atomic<float>& filterCutoff;        // Hz
    const std::atomic<float>& filterResonance;     // 0 to 100 %
    const std::atomic<float>& filterType;          // position in filterTypeNames
    const std::atomic<float>& filterKeyTracking;   // 0 to 100 %
    const std::atomic<float>& filterVelocity;      // octaves, 0 to 8

    // The filter envelope, read on every block.
    const std::atomic<float>& filterAttack;        // seconds
    const std::atomic<float>& filterDecay;         // seconds
    const std::atomic<float>& filterSustain;       // 0 to 100 %
    const std::atomic<float>& filterRelease;       // seconds
    const std::atomic<float>& filterEnvAmount;     // octaves, -8 to 8
};

// One playing note: an oscillator at the note's pitch, scaled by velocity, bent by the pitch wheel
// and the LFO, filtered (when the filter is on, with its cutoff moved by the filter envelope), and shaped over
// time by the amp envelope.
class SynthVoice  : public juce::SynthesiserVoice
{
public:
    // waveformTables holds every waveform the voice can play, and modulation is the synth's LFO. The
    // tables, the modulation and the parameter values must outlive the voice; the struct itself is
    // copied.
    SynthVoice (const WaveformTables& waveformTables, const Modulation& modulation, const VoiceParameters& voiceParameters);

    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;
    void setCurrentPlaybackSampleRate (double newRate) override;

private:
    // A note that was cut off while still sounding: most often because the voice was stolen for a
    // new note when all voices were busy. Instead of stopping dead, which clicks, it carries on
    // underneath the voice's new note, at its last pitch and level, and fades to silence.
    struct FadingNote
    {
        Oscillator oscillator;
        Filter filter;       // in the state it was in, so the note's tone doesn't change as it fades
        bool filterRunning;
        float filterMix;
        std::array<float, 3> filterTypeAmounts;
        double phaseDelta;   // its last pitch, bend and LFO included
        float level;         // its level when it was cut off
        float gain;          // the fade, from 1 down to 0
        float gainStep;
    };

    const WaveformTables& tables;
    const Modulation& modulation;
    VoiceParameters parameters;
    Oscillator oscillator;
    double phaseDelta = 0.0;   // per sample, for the note's unbent pitch; 0 when the voice is idle
    float noteLevel = 0.0f;    // fixed per note: the base level scaled by velocity
    float bendPosition = 0.0f; // the pitch wheel, from -1 (fully down) to 1 (fully up)
    int noteNumber = 60;       // the MIDI note playing, for the filter's key tracking
    float noteVelocity = 1.0f; // the note's velocity, 0 to 1, for velocity → filter
    Envelope ampEnvelope;
    std::optional<FadingNote> fadingNote;

    // Multiplies the note's frequency. Smoothed multiplicatively, so the glide is even in pitch.
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> frequencyRatio { 1.0 };
    float lfoPitchRatio = 1.0f; // the LFO's last effect on the pitch, for a note that's cut off

    // The filter only runs while it's on, or fading in or out. Switching it on or off crossfades
    // between the unfiltered and filtered sound (filterMix 0 to 1), so it doesn't click.
    Filter filter;
    bool filterRunning = false;
    int filterUpdateCountdown = 0; // samples until the filter's coefficients are next worked out
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> filterCutoff { maxCutoffHz };
    juce::SmoothedValue<float> filterResonance; // 0 to 1
    juce::SmoothedValue<float> filterMix;

    // How much of each filter type (indexed by position in filterTypeNames) is heard: 1 for the
    // chosen type and 0 for the others, except while crossfading after the type changes.
    std::array<juce::SmoothedValue<float>, 3> filterTypeAmounts;

    // Moves the cutoff up (or down) by filterEnvAmount octaves at its peak. The envelope itself isn't
    // smoothed, so fast plucks stay sharp; only the amount knob glides.
    Envelope filterEnvelope;
    juce::SmoothedValue<float> filterEnvAmount; // octaves

    void renderFadingNote (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void updateFilterTargets();
    float applyFilter (float input, float filterEnvelopeLevel) noexcept;
    int getCurrentFilterType() const;
    float getFilterCutoffTarget() const;
    double getBendRatio() const;
    const BandLimitedWaveTable& getCurrentTable() const;
    Envelope::Settings getAmpEnvelopeSettings() const;
    Envelope::Settings getFilterEnvelopeSettings() const;
};

} // namespace boringsynth
