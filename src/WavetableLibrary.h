#pragma once

#include "MultiFrameWaveTable.h"

namespace boringsynth
{

// The built-in wavetables, in the order waveformNames lists them from firstWavetable on. Each is worked
// out from a formula, with every frame matched to the sine's RMS, so Position changes the tone and not
// the loudness.
//
// Every frame is an odd function of the phase (a sum of sines), so it's zero at phase 0, where notes
// start, and has no DC offset.
//
// Each frame takes about 90 KB, so together they come to about 15 MB. Building them is the slowest
// thing the plug-in does when it loads, which is why every copy of the plug-in shares one set (see
// WaveformTables).
std::vector<MultiFrameWaveTable> makeWavetables();

} // namespace boringsynth
