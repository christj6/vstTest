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
        frames.emplace_back ([&shape, frame] (double phase) { return shape (frame, phase); });
}

} // namespace boringsynth
