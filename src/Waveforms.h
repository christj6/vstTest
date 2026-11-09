#pragma once

#include "MultiFrameWaveTable.h"

#include <juce_core/juce_core.h>

namespace boringsynth
{

// The waveforms the oscillator can play, in the order the Waveform parameter lists them. A DAW saves
// the choice as its position in this list, so the order is permanent: new waveforms go at the end.
inline const juce::StringArray waveformNames { "Sine", "Triangle", "Saw", "Square", "Sine to Saw", "Pulse Width",
                                               "Hard Sync", "FM Sweep", "FM Hollow", "Wavefold", "Formant", "Harmonics",
                                               "Imported" };
constexpr int sineWaveform = 0, triangleWaveform = 1, sawWaveform = 2, squareWaveform = 3, sineToSawWaveform = 4,
              pulseWidthWaveform = 5, hardSyncWaveform = 6, fmSweepWaveform = 7, fmHollowWaveform = 8, wavefoldWaveform = 9,
              formantWaveform = 10, harmonicsWaveform = 11, importedWaveform = 12;

// The waveforms from here on are wavetables, with several frames for Position to move through. Those
// before have one frame, so Position does nothing to them.
constexpr int firstWavetable = sineToSawWaveform;

// One cycle of each waveform of a single frame at a phase from 0 to 2 pi, from -1 to 1 before loudness
// matching. Every waveform is zero at phase 0, where notes start, so no note starts with a click.
float waveformShape (int waveform, double phase);

// How much each waveform of a single frame is scaled so they all sound about as loud as the sine: each
// is matched to the sine's RMS (average power). The sine itself is unchanged.
float waveformGain (int waveform);

// A table for every waveform. There's only ever one set, shared by every voice of every copy of the
// plug-in (held through a juce::SharedResourcePointer): the first copy to load builds it, and the last
// to go frees it.
class WaveformTables
{
public:
    WaveformTables();

    // The table for a waveform's position in waveformNames. Out-of-range values give the sine, as does
    // Imported, which is each copy of the plug-in's own (see WavetableImporter).
    const MultiFrameWaveTable& get (int waveform) const noexcept;

private:
    std::vector<MultiFrameWaveTable> tables;
};

} // namespace boringsynth
