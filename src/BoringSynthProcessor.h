#pragma once

#include "Parameters.h"
#include "Synth.h"
#include "Waveforms.h"

#include <juce_audio_processors/juce_audio_processors.h>

// A synth that plays one waveform (sine, triangle, saw or square) at the pitch of each held key,
// through an optional resonant filter (low-pass, high-pass or band-pass), shaped by an ADSR amp
// envelope. An LFO moves the pitch (vibrato), the volume (tremolo) or the filter cutoff. Its editor
// is JUCE's generic one, a row for each parameter. The pitch wheel bends notes, the mod wheel can add
// to the LFO's depth, and the sustain pedal holds notes. The parameters are the output volume, velocity sensitivity, pitch bend range,
// waveform, the amp envelope's attack, decay, sustain and release, the filter's on/off switch, type,
// cutoff, resonance, key tracking, velocity amount, and its own envelope's attack, decay, sustain,
// release and amount, the LFO's rate, depth, destination and shape, how much the mod wheel adds to
// the LFO's depth, whether each note restarts the LFO, and whether its rate is synced to the host's
// tempo, as a note length.

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

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

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
    // Declared before the synth, so they outlive the voices that read them.
    WaveformTables waveformTables;
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
    std::atomic<float>* filterKeyTracking = parameters.getRawParameterValue (filterKeyTrackingId);
    std::atomic<float>* filterVelocity = parameters.getRawParameterValue (filterVelocityId);
    std::atomic<float>* filterAttack = parameters.getRawParameterValue (filterAttackId);
    std::atomic<float>* filterDecay = parameters.getRawParameterValue (filterDecayId);
    std::atomic<float>* filterSustain = parameters.getRawParameterValue (filterSustainId);
    std::atomic<float>* filterRelease = parameters.getRawParameterValue (filterReleaseId);
    std::atomic<float>* filterEnvAmount = parameters.getRawParameterValue (filterEnvAmountId);
    std::atomic<float>* lfoRate = parameters.getRawParameterValue (lfoRateId);
    std::atomic<float>* lfoDepth = parameters.getRawParameterValue (lfoDepthId);
    std::atomic<float>* lfoDestination = parameters.getRawParameterValue (lfoDestinationId);
    std::atomic<float>* lfoShape = parameters.getRawParameterValue (lfoShapeId);
    std::atomic<float>* modWheelLfoDepth = parameters.getRawParameterValue (modWheelLfoDepthId);
    std::atomic<float>* lfoRetrigger = parameters.getRawParameterValue (lfoRetriggerId);
    std::atomic<float>* lfoSync = parameters.getRawParameterValue (lfoSyncId);
    std::atomic<float>* lfoSyncRate = parameters.getRawParameterValue (lfoSyncRateId);

    Synth synth { { .rate = *lfoRate,
                    .depth = *lfoDepth,
                    .destination = *lfoDestination,
                    .shape = *lfoShape,
                    .modWheelDepth = *modWheelLfoDepth,
                    .retrigger = *lfoRetrigger,
                    .sync = *lfoSync,
                    .syncRate = *lfoSyncRate } };

    // Smoothed linearly in gain rather than in dB, because the minimum volume is a gain of
    // exactly 0, which multiplicative (dB-style) smoothing can never reach.
    juce::SmoothedValue<float> volumeGain;

    float getVolumeGain() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoringSynthProcessor)
};

} // namespace boringsynth
