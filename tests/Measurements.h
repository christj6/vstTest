#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

// Measurements of rendered audio, shared by every test file.

namespace testing
{

inline float peakOf (const std::vector<float>& samples)
{
    float peak = 0.0f;

    for (auto s : samples)
        peak = std::max (peak, std::abs (s));

    return peak;
}

inline float peakBetween (const std::vector<float>& samples, int start, int end)
{
    return peakOf (std::vector<float> (samples.begin() + start, samples.begin() + end));
}

// The largest change from one sample to the next. A sudden jump in level shows up here.
inline float largestStep (const std::vector<float>& samples)
{
    float largest = 0.0f;

    for (size_t i = 1; i < samples.size(); ++i)
        largest = std::max (largest, std::abs (samples[i] - samples[i - 1]));

    return largest;
}

// The times (in fractional samples) where the waveform crosses zero going upwards, at or after
// sample `from` and before sample `to`. One crossing per cycle of a sine.
inline std::vector<double> upwardCrossings (const std::vector<float>& samples, int from, int to)
{
    std::vector<double> times;

    for (auto i = std::max (from, 1); i < to; ++i)
    {
        const auto before = samples[(size_t) i - 1];
        const auto after = samples[(size_t) i];

        if (before < 0.0f && after >= 0.0f)
            times.push_back (i - 1 + before / (before - after));
    }

    return times;
}

// The frequency of a sine between two sample positions, from its zero crossings.
inline double frequencyBetween (const std::vector<float>& samples, int from, int to, double rate)
{
    const auto times = upwardCrossings (samples, from, to);

    if (times.size() < 2)
        return 0.0;

    return (double) (times.size() - 1) / (times.back() - times.front()) * rate;
}

// The length in samples of the first complete cycle that starts at or after `from`.
inline double firstPeriodAfter (const std::vector<float>& samples, int from)
{
    const auto times = upwardCrossings (samples, from, (int) samples.size());
    return times.size() < 2 ? 0.0 : times[1] - times[0];
}

inline int lastNonZeroIndex (const std::vector<float>& samples)
{
    for (auto i = (int) samples.size() - 1; i >= 0; --i)
        if (samples[(size_t) i] != 0.0f)
            return i;

    return -1;
}

inline double rmsBetween (const std::vector<float>& samples, int start, int end)
{
    double sumOfSquares = 0.0;

    for (auto i = start; i < end; ++i)
        sumOfSquares += (double) samples[(size_t) i] * samples[(size_t) i];

    return std::sqrt (sumOfSquares / (end - start));
}

inline double rmsOf (const std::vector<float>& samples)
{
    return rmsBetween (samples, 0, (int) samples.size());
}

inline double toDecibels (double ratio)
{
    return 20.0 * std::log10 (ratio);
}

// The magnitude of each frequency (0 to size / 2) in a stretch of samples whose length is a power
// of two. When the samples hold exactly one cycle of a waveform, entry k is its harmonic k.
inline std::vector<double> harmonicsOf (const std::vector<float>& samples)
{
    const auto size = (int) samples.size();
    const juce::dsp::FFT fft (juce::roundToInt (std::log2 (size)));
    std::vector<juce::dsp::Complex<float>> in ((size_t) size), out ((size_t) size);

    for (int i = 0; i < size; ++i)
        in[(size_t) i] = samples[(size_t) i];

    fft.perform (in.data(), out.data(), false);

    std::vector<double> magnitudes ((size_t) size / 2 + 1);

    for (size_t k = 0; k < magnitudes.size(); ++k)
        magnitudes[k] = std::abs (out[k]) / (size / 2.0);

    return magnitudes;
}

} // namespace testing
