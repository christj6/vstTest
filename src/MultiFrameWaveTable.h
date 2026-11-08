#pragma once

#include "BandLimitedWaveTable.h"

namespace boringsynth
{

// A waveform as a stack of frames: each frame is one cycle of a different shape, band-limited on its
// own (a BandLimitedWaveTable), so a sound can move from one to the next. A waveform that never
// changes, such as a plain saw, has just one.
//
// Like the tables it's made of, it's built once, never on the audio thread, and shared by every voice.
// Each frame's levels take about 90 KB (11 levels of 2048 samples), so the frames add up.
class MultiFrameWaveTable
{
public:
    // A single frame: a waveform that never changes.
    explicit MultiFrameWaveTable (const std::function<float (double phase)>& shape);

    // numFrames frames, at least one, where shape gives each frame's value at a phase (see WaveTable).
    MultiFrameWaveTable (int numFrames, const std::function<float (int frame, double phase)>& shape);

    int getNumFrames() const noexcept { return (int) frames.size(); }

    const BandLimitedWaveTable& getFrame (int frame) const noexcept { return frames[(size_t) frame]; }

private:
    std::vector<BandLimitedWaveTable> frames;
};

} // namespace boringsynth
