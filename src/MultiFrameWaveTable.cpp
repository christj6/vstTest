#include "MultiFrameWaveTable.h"

namespace boringsynth
{

MultiFrameWaveTable::MultiFrameWaveTable (const std::vector<std::vector<float>>& cycles)
{
    jassert (! cycles.empty());
    frames.reserve (cycles.size());

    for (const auto& cycle : cycles)
    {
        frames.emplace_back (cycle);

        for (int level = 0; level < BandLimitedWaveTable::numLevels; ++level)
            everyFrameStartsAtZero = everyFrameStartsAtZero && std::abs (frames.back().getLevel (level).getSample (0.0)) < 1.0e-4f;
    }
}

} // namespace boringsynth
