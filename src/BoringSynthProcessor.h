#pragma once

#include "Parameters.h"
#include "Synth.h"
#include "Waveforms.h"

#include <juce_audio_processors/juce_audio_processors.h>

// A synth that plays one waveform (sine, triangle, saw or square) at the pitch of each held key,
// through an optional resonant filter (low-pass, high-pass or band-pass), shaped by an ADSR amp
// envelope. No editor yet. The pitch wheel bends notes and the sustain pedal holds them. The
// parameters are the output volume, velocity sensitivity, pitch bend range, waveform, the amp
// envelope's attack, decay, sustain and release, and the filter's on/off switch, type, cutoff and
// resonance.

namespace boringsynth
{

class BoringSynthProcessor  : public juce::AudioProcessor
{
public:
    BoringSynthProcessor();

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    // Declared before the synth, so it outlives the voices that read it.
    WaveformTables waveformTables;

    Synth synth;
    juce::AudioProcessorValueTreeState parameters { *this, nullptr, stateTag, createParameterLayout() };
    std::atomic<float>* volumeDb = parameters.getRawParameterValue (volumeId);
    std::atomic<float>* velocitySensitivity = parameters.getRawParameterValue (velocitySensitivityId);
    std::atomic<float>* pitchBendRange = parameters.getRawParameterValue (pitchBendRangeId);
    std::atomic<float>* waveform = parameters.getRawParameterValue (waveformId);
    std::atomic<float>* ampAttack = parameters.getRawParameterValue (ampAttackId);
    std::atomic<float>* ampDecay = parameters.getRawParameterValue (ampDecayId);
    std::atomic<float>* ampSustain = parameters.getRawParameterValue (ampSustainId);
    std::atomic<float>* ampRelease = parameters.getRawParameterValue (ampReleaseId);
    std::atomic<float>* filterEnabled = parameters.getRawParameterValue (filterEnabledId);
    std::atomic<float>* filterCutoff = parameters.getRawParameterValue (filterCutoffId);
    std::atomic<float>* filterResonance = parameters.getRawParameterValue (filterResonanceId);
    std::atomic<float>* filterType = parameters.getRawParameterValue (filterTypeId);

    // Smoothed linearly in gain rather than in dB, because the minimum volume is a gain of
    // exactly 0, which multiplicative (dB-style) smoothing can never reach.
    juce::SmoothedValue<float> volumeGain;

    float getVolumeGain() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoringSynthProcessor)
};

} // namespace boringsynth
