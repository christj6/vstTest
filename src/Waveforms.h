#pragma once

#include "BandLimitedWaveTable.h"

#include <juce_core/juce_core.h>

namespace boringsynth
{

// The waveforms the oscillator can play, in the order the Waveform parameter lists them. A DAW saves
// the choice as its position in this list, so the order is permanent: new waveforms go at the end.
inline const juce::StringArray waveformNames { "Sine", "Triangle", "Saw", "Square" };
constexpr int sineWaveform = 0, triangleWaveform = 1, sawWaveform = 2, squareWaveform = 3;

// One cycle of each waveform at a phase from 0 to 2 pi, from -1 to 1 before loudness matching.
// Every waveform is zero at phase 0, where notes start, so no note starts with a click.
float waveformShape (int waveform, double phase);

// How much each waveform is scaled so they all sound about as loud as the sine: each is matched to
// the sine's RMS (average power). The sine itself is unchanged.
float waveformGain (int waveform);

// A band-limited table for every waveform, built once when the plug-in loads and shared by every
// voice.
class WaveformTables
{
public:
    WaveformTables();

    // The table for a waveform's position in waveformNames. Out-of-range values give the sine.
    const BandLimitedWaveTable& get (int waveform) const noexcept;

private:
    std::vector<BandLimitedWaveTable> tables;
};

// A plain table for every waveform, for the LFO: each shape exactly as waveformShape gives it, so
// every one stays within -1 to 1 and reaches both. They aren't loudness-matched, which scales the
// triangle and saw past 1 and keeps the square short of it. They aren't band-limited either: an LFO
// is far too slow to alias, and the levels with fewer harmonics overshoot the saw's and square's
// edges. Built once when the plug-in loads.
class LfoShapeTables
{
public:
    LfoShapeTables();

    // The table for a waveform's position in waveformNames. Out-of-range values give the sine.
    const WaveTable& get (int waveform) const noexcept;

private:
    std::vector<WaveTable> tables;
};

} // namespace boringsynth
