#include "Waveforms.h"

namespace boringsynth
{

float waveformShape (int waveform, double phase)
{
    constexpr auto pi = juce::MathConstants<double>::pi;

    switch (waveform)
    {
        // Rises from 0 to 1 over the first quarter of the cycle, falls to -1 by the third quarter,
        // and rises back to 0.
        case 1:
            return (float) (2.0 / pi * std::asin (std::sin (phase)));

        // Rises from 0 to 1 over the first half of the cycle, jumps to -1, and rises back to 0.
        // The sample on the jump is its midpoint, 0, which is what band-limiting settles on anyway.
        case 2:
            if (phase == pi)
                return 0.0f;

            return (float) (phase < pi ? phase / pi : phase / pi - 2.0);

        // 1 for the first half of the cycle and -1 for the second, with the jumps (at 0 and half
        // way) at their midpoint, 0.
        case 3:
            if (phase == 0.0 || phase == pi)
                return 0.0f;

            return phase < pi ? 1.0f : -1.0f;

        default:
            return (float) std::sin (phase);
    }
}

float waveformGain (int waveform)
{
    // A sine's RMS is 1 / sqrt (2). A triangle's and a sawtooth's are 1 / sqrt (3), and a square's
    // is 1, so each is scaled by the sine's RMS divided by its own.
    switch (waveform)
    {
        case 1:
        case 2:  return (float) std::sqrt (1.5);
        case 3:  return (float) std::sqrt (0.5);
        default: return 1.0f;
    }
}

WaveformTables::WaveformTables()
{
    tables.reserve ((size_t) waveformNames.size());

    for (int waveform = 0; waveform < waveformNames.size(); ++waveform)
    {
        const auto gain = waveformGain (waveform);
        tables.emplace_back ([waveform, gain] (double phase) { return gain * waveformShape (waveform, phase); });
    }
}

const BandLimitedWaveTable& WaveformTables::get (int waveform) const noexcept
{
    return tables[(size_t) (juce::isPositiveAndBelow (waveform, (int) tables.size()) ? waveform : 0)];
}

} // namespace boringsynth
