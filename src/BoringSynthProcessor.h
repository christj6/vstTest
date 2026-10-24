#pragma once

#include "Parameters.h"
#include "Synth.h"
#include "BandLimitedWaveTable.h"

#include <juce_audio_processors/juce_audio_processors.h>

// A synth that does nothing but play a sine wave at the pitch of each held key.
// No envelope and no editor: note on starts the sine, note off stops it (with a 5 ms fade so the
// cut doesn't click). The pitch wheel bends notes and the sustain pedal holds them. The parameters
// are the output volume, velocity sensitivity and pitch bend range.

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
    BandLimitedWaveTable sineTable { [] (double phase) { return (float) std::sin (phase); } };

    Synth synth;
    juce::AudioProcessorValueTreeState parameters { *this, nullptr, stateTag, createParameterLayout() };
    std::atomic<float>* volumeDb = parameters.getRawParameterValue (volumeId);
    std::atomic<float>* velocitySensitivity = parameters.getRawParameterValue (velocitySensitivityId);
    std::atomic<float>* pitchBendRange = parameters.getRawParameterValue (pitchBendRangeId);

    // Smoothed linearly in gain rather than in dB, because the minimum volume is a gain of
    // exactly 0, which multiplicative (dB-style) smoothing can never reach.
    juce::SmoothedValue<float> volumeGain;

    float getVolumeGain() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoringSynthProcessor)
};

} // namespace boringsynth
