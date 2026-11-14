#include "WaveTable.h"

namespace boringsynth
{

std::vector<float> sampleCycle (const std::function<float (double phase)>& shape)
{
    std::vector<float> cycle ((size_t) WaveTable::size);

    for (int i = 0; i < WaveTable::size; ++i)
        cycle[(size_t) i] = shape (juce::MathConstants<double>::twoPi * i / WaveTable::size);

    return cycle;
}

WaveTable::WaveTable (const std::vector<float>& cycle)
    : samples (cycle)
{
    jassert ((int) cycle.size() == size);
    samples.resize ((size_t) size + 1);
    samples[(size_t) size] = samples[0];
}

} // namespace boringsynth
