#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace boringsynth
{
// Produces a sine wave one sample at a time. It holds only the wave's phase; the caller decides the
// pitch on every sample by passing how far the phase moves, which is how pitch bends glide.
class Oscillator
{
public:
    // Starts the wave again from the beginning of its cycle, where a sine is at zero.
    void reset() noexcept { phase = 0.0; }

    // Returns the next sample (-1 to 1) and moves the phase on by phaseDelta radians, which is
    // 2 pi times the frequency divided by the sample rate.
    //
    // At or above the Nyquist frequency (half the sample rate, a phaseDelta of pi) the wave would
    // alias, so it's silent there instead. The phase holds still, so the wave carries on smoothly
    // if the pitch comes back down, and it never moves by more than pi per sample, so wrapping it
    // once is always enough.
    float getNextSample (double phaseDelta) noexcept
    {
        if (phaseDelta >= juce::MathConstants<double>::pi)
            return 0.0f;

        const auto sample = (float) std::sin (phase);
        phase += phaseDelta;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        return sample;
    }

private:
    double phase = 0.0;
};
} // namespace boringsynth
