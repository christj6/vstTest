#include "SynthVoice.h"

namespace boringsynth
{

namespace
{

// Fixed output level per note. Eight notes peaking together reach 0.8, below full scale.
constexpr float level = 0.1f;

// How long the pitch takes to follow the wheel. Many keyboards send the wheel in coarse steps;
// this smooths them out while still feeling immediate.
constexpr double bendSmoothingSeconds = 0.01;

// A note's level for its velocity (0 to 1) and the velocity sensitivity (0 to 1). At full
// sensitivity this is the General MIDI curve, velocity squared, where each halving of velocity is
// about -12 dB. At zero sensitivity velocity is ignored. The hardest hit is always full level.
float velocityToGain (float velocity, float sensitivity)
{
    return (1.0f - sensitivity) + sensitivity * velocity * velocity;
}

// Converts the MIDI pitch wheel (0 to 16383, centre 8192) to -1 (fully down) to 1 (fully up).
// The two halves are scaled separately because the wheel has one more step below the centre than
// above it, and both extremes should reach the full bend range.
float pitchWheelToBend (int wheelValue)
{
    const auto offset = (float) (wheelValue - 8192);
    return offset >= 0.0f ? offset / 8191.0f : offset / 8192.0f;
}

} // namespace

SynthVoice::SynthVoice (const WaveTable& waveTable, const VoiceParameters& voiceParameters)
    : parameters (voiceParameters),
      oscillator (waveTable)
{
}

bool SynthVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<SynthSound*> (sound) != nullptr;
}

void SynthVoice::startNote (int midiNote, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition)
{
    const auto sampleRate = getSampleRate();

    // Stay silent if the host hasn't given us a sample rate yet.
    if (sampleRate <= 0.0)
    {
        clearCurrentNote();
        phaseDelta = 0.0;
        return;
    }

    oscillator.reset();
    phaseDelta = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (midiNote) / sampleRate;
    noteLevel = level * velocityToGain (velocity, parameters.velocitySensitivity.load() / 100.0f);
    gain = 1.0f;
    gainStep = 0.0f;

    // A note played while the wheel is already pushed starts at the bent pitch, not gliding to it.
    bendPosition = pitchWheelToBend (currentPitchWheelPosition);
    frequencyRatio.reset (sampleRate, bendSmoothingSeconds);
    frequencyRatio.setCurrentAndTargetValue (getBendRatio());
}

void SynthVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        gainStep = (float) (1.0 / (releaseSeconds * getSampleRate()));
        return;
    }

    clearCurrentNote();
    phaseDelta = 0.0;
}

void SynthVoice::pitchWheelMoved (int newPitchWheelValue)
{
    bendPosition = pitchWheelToBend (newPitchWheelValue);
}

void SynthVoice::renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (phaseDelta == 0.0)
        return;

    // Picks up both wheel moves and changes to the bend range; either way the pitch glides.
    frequencyRatio.setTargetValue (getBendRatio());

    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        const auto sample = gain * noteLevel * oscillator.getNextSample (phaseDelta * frequencyRatio.getNextValue());

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, i, sample);

        gain -= gainStep;

        if (gain <= 0.0f)
        {
            clearCurrentNote();
            phaseDelta = 0.0;
            break;
        }
    }
}

double SynthVoice::getBendRatio() const
{
    return std::exp2 (bendPosition * parameters.pitchBendRange.load() / 12.0);
}

} // namespace boringsynth
