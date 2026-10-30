#pragma once

#include "WaveTable.h"

namespace boringsynth
{

// A waveform stored as a ladder of WaveTables, each with fewer high harmonics than the last, so that
// any note can play without its harmonics passing the Nyquist frequency.
//
// A harmonic above the Nyquist frequency (half the sample rate) can't be reproduced; it "folds back"
// and is heard as a wrong, unrelated pitch. A high note has room for fewer harmonics below Nyquist
// than a low one, so each note reads the richest version whose harmonics all fit.
//
// The versions are built once, when the plug-in loads: an FFT splits the waveform into its
// harmonics, the ones above each version's limit are removed, and an inverse FFT turns the rest
// back into a waveform.
class BandLimitedWaveTable
{
public:
    // Level 0 keeps every harmonic the table can hold; each level after keeps at most half as many,
    // down to the last, which keeps only the fundamental.
    static constexpr int numLevels = 11;

    // Builds every level from one cycle of `shape` (see WaveTable).
    explicit BandLimitedWaveTable (const std::function<float (double phase)>& shape);

    // The richest level with no harmonics at or above the Nyquist frequency, for a note whose phase
    // moves by phaseDelta radians per sample (2 pi times the frequency divided by the sample rate).
    const WaveTable& getTableFor (double phaseDelta) const noexcept;

    // The most harmonics a level keeps: 1023 at level 0, then 512, 256 and so on down to 1.
    static int getHarmonicLimit (int level) noexcept;

    const WaveTable& getLevel (int level) const noexcept { return levels[(size_t) level]; }

private:
    std::vector<WaveTable> levels;
};

} // namespace boringsynth
