#include "WaveTable.h"

namespace boringsynth
{

WaveTable::WaveTable (const std::function<float (double phase)>& shape)
    : samples ((size_t) size + 1)
{
    for (int i = 0; i < size; ++i)
        samples[(size_t) i] = shape (juce::MathConstants<double>::twoPi * i / size);

    samples[(size_t) size] = samples[0];
}

} // namespace boringsynth
