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

    // The waveform's value at a phase from 0 up to 2 pi, from one band-limited level (see
    // BandLimitedWaveTable::getLevelFor), at a position from 0 (the first frame) to 1 (the last).
    // Between two frames it blends them, sample by sample. Exactly on a frame it reads only that frame,
    // so a single frame gives exactly its own values.
    float getSample (float position, int level, double phase) const noexcept
    {
        const auto lastFrame = getNumFrames() - 1;

        // A glide's rounding can leave position a hair past 1, which mustn't reach past the last frame.
        const auto place = std::min (position * (float) lastFrame, (float) lastFrame);
        const auto frame = (int) place;
        const auto fraction = place - (float) frame;
        const auto sample = frames[(size_t) frame].getLevel (level).getSample (phase);

        if (fraction <= 0.0f)
            return sample;

        return sample + fraction * (frames[(size_t) frame + 1].getLevel (level).getSample (phase) - sample);
    }

private:
    std::vector<BandLimitedWaveTable> frames;
};

} // namespace boringsynth
