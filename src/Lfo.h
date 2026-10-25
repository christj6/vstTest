#pragma once

#include "WaveTable.h"

namespace boringsynth
{

// The LFO's wave, one sample at a time: a plain WaveTable (one of the LfoShapeTables) read at a slow
// rate. Like Oscillator, it holds only the phase, and the caller passes how far it moves on each
// sample. Unlike Oscillator it needs no band-limiting: an LFO never gets anywhere near the Nyquist
// frequency.
class Lfo
{
public:
    // The table must outlive the LFO. It's shared, so the LFO never changes it.
    explicit Lfo (const WaveTable& shape) : table (&shape) {}

    // Starts the wave again from the beginning of its cycle.
    void reset() noexcept { phase = 0.0; }

    // Returns the next value (-1 to 1) and moves the phase on by phaseDelta radians, which is 2 pi
    // times the LFO's rate divided by the sample rate. LFO rates are far below the sample rate, so
    // the phase never moves by more than a cycle per sample, and wrapping it once is always enough.
    float getNextSample (double phaseDelta) noexcept
    {
        const auto value = table->getSample (phase);
        phase += phaseDelta;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        return value;
    }

private:
    const WaveTable* table;
    double phase = 0.0;
};

} // namespace boringsynth
