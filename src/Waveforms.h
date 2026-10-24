#pragma once

#include "BandLimitedWaveTable.h"

#include <juce_core/juce_core.h>

namespace boringsynth
{

// The waveforms the oscillator can play, in the order the Waveform parameter lists them. A DAW saves
// the choice as its position in this list, so the order is permanent: new waveforms go at the end.
inline const juce::StringArray waveformNames { "Sine", "Triangle", "Saw", "Square" };

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

} // namespace boringsynth
