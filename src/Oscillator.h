#pragma once

#include "MultiFrameWaveTable.h"

namespace boringsynth
{

// Produces a waveform one sample at a time by reading a MultiFrameWaveTable. It holds only the wave's
// phase; the caller decides the pitch and where to read in the table's frames on every sample, which
// is how pitch bends and Position glide. Each sample reads the table level that suits that pitch, so
// none of the waveform's harmonics go past the Nyquist frequency. That's the aliasing that matters;
// reading between a table's points along straight lines leaves faint errors of its own (about -118 dB
// for a sine), and whatever comes after, such as the output's clipping, can add more.
class Oscillator
{
public:
    // Tables must outlive the oscillator. They're shared, so the oscillator never changes them.
    explicit Oscillator (const MultiFrameWaveTable& waveTable) : table (&waveTable) {}

    // Switches to another waveform. With crossfadeSamples of 0 it switches at once, which is right
    // when a note starts, and ends any fade still under way, even to the same table: the table it was
    // fading from mustn't be read again. Otherwise it fades from the old waveform to the new one over
    // that many samples, reading both at the same phase, so a change during a note doesn't click.
    void setTable (const MultiFrameWaveTable& newTable, int crossfadeSamples) noexcept
    {
        if (crossfadeSamples <= 0)
        {
            table = &newTable;
            previousTable = nullptr;
            fadeRemaining = 0;
            return;
        }

        if (&newTable == table)
            return;

        previousTable = table;
        table = &newTable;
        fadeLength = crossfadeSamples;
        fadeRemaining = crossfadeSamples;
    }

    // Starts the wave again from the beginning of its cycle.
    void reset() noexcept { phase = 0.0; }

    // Returns the next sample (-1 to 1) and moves the phase on by phaseDelta radians, which is
    // 2 pi times the frequency divided by the sample rate. position is where to read in the table's
    // frames, from 0 (the first) to 1 (the last).
    //
    // At or above the Nyquist frequency (half the sample rate, a phaseDelta of pi) the wave would
    // alias, so it's silent there instead. The phase holds still, so the wave carries on smoothly
    // if the pitch comes back down, and it never moves by more than pi per sample, so wrapping it
    // once is always enough.
    float getNextSample (double phaseDelta, float position = 0.0f) noexcept
    {
        if (phaseDelta >= juce::MathConstants<double>::pi)
            return 0.0f;

        // Finding the level takes a search, and a held note's pitch rarely changes.
        if (phaseDelta != levelPhaseDelta)
        {
            level = BandLimitedWaveTable::getLevelFor (phaseDelta);
            levelPhaseDelta = phaseDelta;
        }

        auto sample = table->getSample (position, level, phase);

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
                const auto previousSample = previousTable->getSample (position, level, phase);
                sample = previousSample + progress * (sample - previousSample);
            }
        }

        phase += phaseDelta;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        return sample;
    }

private:
    const MultiFrameWaveTable* table;
    const MultiFrameWaveTable* previousTable = nullptr; // the waveform being faded out, if any
    int fadeLength = 0;    // samples the current crossfade lasts
    int fadeRemaining = 0; // samples of it still to go
    double phase = 0.0;
    int level = 0;                // the band-limited level for the pitch below
    double levelPhaseDelta = 0.0; // 0 finds level 0, so it's right before the first search too
};

} // namespace boringsynth
