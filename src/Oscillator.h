#pragma once

#include "BandLimitedWaveTable.h"

namespace boringsynth
{

// Produces a waveform one sample at a time by reading a BandLimitedWaveTable. It holds only the
// wave's phase; the caller decides the pitch on every sample by passing how far the phase moves,
// which is how pitch bends glide. Each sample reads the table level that suits that pitch, so the
// waveform never aliases.
class Oscillator
{
public:
    // Tables must outlive the oscillator. They're shared, so the oscillator never changes them.
    explicit Oscillator (const BandLimitedWaveTable& waveTable) : table (&waveTable) {}

    // Switches to another waveform. With crossfadeSamples of 0 it switches at once, which is right
    // when a note starts. Otherwise it fades from the old waveform to the new one over that many
    // samples, reading both at the same phase, so a change during a note doesn't click.
    void setTable (const BandLimitedWaveTable& newTable, int crossfadeSamples) noexcept
    {
        if (&newTable == table)
            return;

        previousTable = crossfadeSamples > 0 ? table : nullptr;
        table = &newTable;
        fadeLength = crossfadeSamples;
        fadeRemaining = crossfadeSamples;
    }

    // Starts the wave again from the beginning of its cycle.
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

        auto sample = table->getTableFor (phaseDelta).getSample (phase);

        if (previousTable != nullptr)
        {
            // Counted in whole samples, so the fade ends on exactly its last sample.
            if (--fadeRemaining <= 0)
            {
                previousTable = nullptr;
            }
            else
            {
                const auto progress = (float) (fadeLength - fadeRemaining) / (float) fadeLength;
                const auto previousSample = previousTable->getTableFor (phaseDelta).getSample (phase);
                sample = previousSample + progress * (sample - previousSample);
            }
        }

        phase += phaseDelta;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        return sample;
    }

private:
    const BandLimitedWaveTable* table;
    const BandLimitedWaveTable* previousTable = nullptr; // the waveform being faded out, if any
    int fadeLength = 0;    // samples the current crossfade lasts
    int fadeRemaining = 0; // samples of it still to go
    double phase = 0.0;
};

} // namespace boringsynth
