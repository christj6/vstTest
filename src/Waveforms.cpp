#include "Waveforms.h"

#include <array>

namespace boringsynth
{

namespace
{

// Sine to Saw's frames. Blending neighbouring ones would give nearly the same sound with only two, but
// each is matched in loudness, which a straight blend of the two ends isn't (about 0.5 dB low midway).
constexpr int sineToSawFrames = 8;

// A sine blended with a saw matched to its loudness, in a straight line, from all sine at 0 to all saw
// at 1. Both are zero at phase 0, so every blend is too.
double sineToSawBlend (double amount, double phase)
{
    const auto sine = (double) waveformShape (sineWaveform, phase);
    const auto saw = (double) (waveformGain (sawWaveform) * waveformShape (sawWaveform, phase));
    return sine + amount * (saw - sine);
}

MultiFrameWaveTable makeSineToSaw()
{
    // What scales each frame to the sine's RMS, 1 / sqrt (2).
    std::array<double, sineToSawFrames> gains;

    for (int frame = 0; frame < sineToSawFrames; ++frame)
    {
        const auto amount = (double) frame / (sineToSawFrames - 1);
        double sumOfSquares = 0.0;

        for (int i = 0; i < WaveTable::size; ++i)
        {
            const auto value = sineToSawBlend (amount, juce::MathConstants<double>::twoPi * i / WaveTable::size);
            sumOfSquares += value * value;
        }

        gains[(size_t) frame] = std::sqrt (0.5 / (sumOfSquares / WaveTable::size));
    }

    return { sineToSawFrames, [gains] (int frame, double phase)
    {
        return (float) (gains[(size_t) frame] * sineToSawBlend ((double) frame / (sineToSawFrames - 1), phase));
    } };
}

} // namespace

float waveformShape (int waveform, double phase)
{
    constexpr auto pi = juce::MathConstants<double>::pi;

    switch (waveform)
    {
        // Rises from 0 to 1 over the first quarter of the cycle, falls to -1 by the third quarter,
        // and rises back to 0.
        case triangleWaveform:
            return (float) (2.0 / pi * std::asin (std::sin (phase)));

        // Rises from 0 to 1 over the first half of the cycle, jumps to -1, and rises back to 0.
        // The sample on the jump is its midpoint, 0, which is what band-limiting settles on anyway.
        case sawWaveform:
            if (phase == pi)
                return 0.0f;

            return (float) (phase < pi ? phase / pi : phase / pi - 2.0);

        // 1 for the first half of the cycle and -1 for the second, with the jumps (at 0 and half
        // way) at their midpoint, 0.
        case squareWaveform:
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
        case triangleWaveform:
        case sawWaveform:    return (float) std::sqrt (1.5);
        case squareWaveform: return (float) std::sqrt (0.5);
        default: return 1.0f;
    }
}

WaveformTables::WaveformTables()
{
    tables.reserve ((size_t) waveformNames.size());

    for (int waveform = 0; waveform < firstWavetable; ++waveform)
    {
        const auto gain = waveformGain (waveform);
        tables.emplace_back ([waveform, gain] (double phase) { return gain * waveformShape (waveform, phase); });
    }

    tables.push_back (makeSineToSaw());
    jassert ((int) tables.size() == waveformNames.size());
}

const MultiFrameWaveTable& WaveformTables::get (int waveform) const noexcept
{
    return tables[(size_t) (juce::isPositiveAndBelow (waveform, (int) tables.size()) ? waveform : sineWaveform)];
}

} // namespace boringsynth
