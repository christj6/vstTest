#include "MultiFrameWaveTable.h"

namespace boringsynth
{

MultiFrameWaveTable::MultiFrameWaveTable (const std::function<float (double phase)>& shape)
    : MultiFrameWaveTable (1, [&shape] (int, double phase) { return shape (phase); })
{
}

MultiFrameWaveTable::MultiFrameWaveTable (int numFrames, const std::function<float (int frame, double phase)>& shape)
{
    jassert (numFrames > 0);
    frames.reserve ((size_t) numFrames);

    for (int frame = 0; frame < numFrames; ++frame)
    {
        frames.emplace_back ([&shape, frame] (double phase) { return shape (frame, phase); });

        for (int level = 0; level < BandLimitedWaveTable::numLevels; ++level)
            everyFrameStartsAtZero = everyFrameStartsAtZero && std::abs (frames.back().getLevel (level).getSample (0.0)) < 1.0e-4f;
    }
}

// The frames ask for exactly the samples' phases, so each lands on one of the samples given.
MultiFrameWaveTable::MultiFrameWaveTable (const std::vector<std::vector<float>>& cycles)
    : MultiFrameWaveTable ((int) cycles.size(), [&cycles] (int frame, double phase)
      {
          return cycles[(size_t) frame][(size_t) juce::roundToInt (phase * WaveTable::size / juce::MathConstants<double>::twoPi)];
      })
{
}

} // namespace boringsynth
