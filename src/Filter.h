#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace boringsynth
{

// A resonant two-pole (12 dB per octave) state-variable filter, in the "topology-preserving
// transform" form described by Andrew Simper (Cytomic). It stays stable at any cutoff and
// resonance, even when they change on every sample, and gives low-pass, band-pass and high-pass
// outputs from the same calculation.
//
// It's a plain class of a few numbers, so copying it never allocates. That matters because a
// stolen voice's fading note takes a copy of its filter on the audio thread.
class Filter
{
public:
    // Resonance as Q: 1 / sqrt (2) is a flat response with no peak; higher values peak at the cutoff.
    static constexpr float minQ = 0.70710678f;
    static constexpr float maxQ = 10.0f;

    // Resonance from 0 to 1 (0 to 100 %) as Q, spread evenly on a log scale from flat (0) to a
    // +20 dB peak (1), which is how much the resonance knob seems to change by ear.
    static float resonanceToQ (float resonance) noexcept
    {
        return minQ * std::pow (maxQ / minQ, std::clamp (resonance, 0.0f, 1.0f));
    }

    void setSampleRate (double newSampleRate) noexcept { sampleRate = newSampleRate; }

    // The cutoff is limited to just below the Nyquist frequency, where the filter can't work.
    void setCutoffAndQ (float cutoffHz, float q) noexcept
    {
        if (sampleRate <= 0.0)
            return;

        const auto cutoff = std::clamp ((double) cutoffHz, 1.0, 0.45 * sampleRate);
        const auto g = std::tan (juce::MathConstants<double>::pi * cutoff / sampleRate);
        k = (float) (1.0 / std::max (q, minQ * 0.5f));
        a1 = (float) (1.0 / (1.0 + g * (g + k)));
        a2 = (float) g * a1;
        a3 = (float) g * a2;
    }

    // Clears what the filter remembers of earlier input, so the next sample starts it fresh.
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    // All three kinds of filtering of one sample, from the same calculation. At the cutoff each has a
    // gain of Q: -3 dB with no resonance, rising to +20 dB at full resonance. Low-pass and high-pass
    // fall 12 dB per octave away from the cutoff; band-pass falls 6 dB per octave either side.
    struct Outputs
    {
        float lowPass;
        float bandPass;
        float highPass;
    };

    // Filters one sample. Call exactly one of these per sample.
    Outputs processAll (float input) noexcept       { process (input); return { low, band, input - k * band - low }; }
    float processLowPass (float input) noexcept     { process (input); return low; }
    float processBandPass (float input) noexcept    { process (input); return band; }
    float processHighPass (float input) noexcept    { process (input); return input - k * band - low; }

private:
    void process (float input) noexcept
    {
        const auto v3 = input - ic2;
        band = a1 * ic1 + a2 * v3;
        low = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * band - ic1;
        ic2 = 2.0f * low - ic2;
    }

    double sampleRate = 0.0;
    float k = 1.0f / minQ;               // 1 / Q
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;        // the filter's memory of earlier samples
    float low = 0.0f, band = 0.0f;
};

} // namespace boringsynth
