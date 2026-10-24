#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace boringsynth
{

// One cycle of a waveform, stored as evenly spaced samples. Reading between two samples blends
// them (linear interpolation), so the table can be read at any phase.
//
// Building a table takes a little time and memory, so it's done once, when the plug-in loads, never
// on the audio thread. Every voice then shares the same table.
class WaveTable
{
public:
    // Samples per cycle. With linear interpolation, a sine read from 2048 samples is within about
    // 1.2e-6 of a true sine (about -118 dB below full scale), far below anything audible.
    static constexpr int size = 2048;

    // Fills the table by calling shape at each sample's phase, from 0 up to (but not including)
    // 2 pi. shape returns the waveform's value at that phase, from -1 to 1.
    explicit WaveTable (const std::function<float (double phase)>& shape);

    // The waveform's value at a phase from 0 up to 2 pi.
    float getSample (double phase) const noexcept
    {
        const auto position = phase * (size / juce::MathConstants<double>::twoPi);

        // A phase a hair under 2 pi can round up to exactly `size`; reading sample size - 1 with
        // a fraction of 1 gives the right answer then, and stays inside the table.
        const auto index = std::min ((int) position, size - 1);
        const auto fraction = (float) (position - index);

        return samples[(size_t) index] + fraction * (samples[(size_t) index + 1] - samples[(size_t) index]);
    }

private:
    // size + 1 samples: the last repeats the first, so blending the final sample with the next
    // one never has to wrap around to the start.
    std::vector<float> samples;
};

} // namespace boringsynth
