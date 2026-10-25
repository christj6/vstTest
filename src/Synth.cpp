#include "Synth.h"

#include "Parameters.h"

namespace boringsynth
{

namespace
{

// How long the LFO's depth takes to follow its knob, so turning it doesn't make the pitch jump.
constexpr double lfoDepthSmoothingSeconds = 0.02;

// How far the LFO moves the pitch, in semitones either way, for a depth from 0 to 1. Squared, so
// most of the knob goes to the small depths vibrato uses: 20 % is about half a semitone, and 100 %
// is the full octave. At a depth of 0 it's exactly 0.
float lfoPitchSemitones (float depth) noexcept
{
    return maxLfoPitchSemitones * depth * depth;
}

} // namespace

Synth::Synth (const BandLimitedWaveTable& lfoShape, const LfoParameters& lfoParameters)
    : parameters (lfoParameters),
      lfo (lfoShape)
{
}

void Synth::setCurrentPlaybackSampleRate (double newRate)
{
    Synthesiser::setCurrentPlaybackSampleRate (newRate);

    // The LFO starts again from the beginning of its cycle, at the current depth rather than
    // gliding to it.
    lfo.reset();
    lfoDepth.reset (newRate, lfoDepthSmoothingSeconds);
    lfoDepth.setCurrentAndTargetValue (parameters.depth.load() / 100.0f);
}

void Synth::renderVoices (juce::AudioBuffer<float>& outputAudio, int startSample, int numSamples)
{
    const auto currentSampleRate = getSampleRate();
    const auto lfoPhaseDelta = currentSampleRate > 0.0 ? juce::MathConstants<double>::twoPi * parameters.rate.load() / currentSampleRate
                                                       : 0.0;
    lfoDepth.setTargetValue (parameters.depth.load() / 100.0f);

    // The LFO moves on one sample at a time, in order, so the voices hear the same LFO whatever
    // size of block the host sends.
    while (numSamples > 0)
    {
        const auto numThisTime = std::min (numSamples, Modulation::maxSamples);
        modulation.startSample = startSample;

        for (int i = 0; i < numThisTime; ++i)
        {
            const auto semitones = lfoPitchSemitones (lfoDepth.getNextValue());
            modulation.pitchRatios[(size_t) i] = std::exp2 (semitones * lfo.getNextSample (lfoPhaseDelta) / 12.0f);
        }

        Synthesiser::renderVoices (outputAudio, startSample, numThisTime);
        startSample += numThisTime;
        numSamples -= numThisTime;
    }
}

} // namespace boringsynth
