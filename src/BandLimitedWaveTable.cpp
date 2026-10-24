#include "BandLimitedWaveTable.h"

#include <juce_dsp/juce_dsp.h>

namespace boringsynth
{

namespace
{

constexpr int fftOrder = 11;
static_assert ((1 << fftOrder) == WaveTable::size, "the FFT must be the same size as a wave table");

} // namespace

BandLimitedWaveTable::BandLimitedWaveTable (const std::function<float (double phase)>& shape)
{
    constexpr auto size = WaveTable::size;
    const juce::dsp::FFT fft (fftOrder);

    // Split one cycle of the waveform into its harmonics. Bin k of the spectrum is harmonic k, and
    // bin size - k is its mirror image, which a real waveform needs as well.
    std::vector<juce::dsp::Complex<float>> cycle ((size_t) size), spectrum ((size_t) size);

    for (int i = 0; i < size; ++i)
        cycle[(size_t) i] = shape (juce::MathConstants<double>::twoPi * i / size);

    fft.perform (cycle.data(), spectrum.data(), false);

    std::vector<juce::dsp::Complex<float>> limitedSpectrum ((size_t) size), limitedCycle ((size_t) size);
    std::vector<float> samples ((size_t) size);
    levels.reserve ((size_t) numLevels);

    for (int level = 0; level < numLevels; ++level)
    {
        const auto limit = getHarmonicLimit (level);

        for (int bin = 0; bin < size; ++bin)
        {
            const auto harmonic = std::min (bin, size - bin);
            limitedSpectrum[(size_t) bin] = harmonic <= limit ? spectrum[(size_t) bin] : juce::dsp::Complex<float>();
        }

        // JUCE's inverse FFT divides by the size, so this gives back the waveform at its original level.
        fft.perform (limitedSpectrum.data(), limitedCycle.data(), true);

        for (int i = 0; i < size; ++i)
            samples[(size_t) i] = limitedCycle[(size_t) i].real();

        levels.emplace_back (samples);
    }
}

const WaveTable& BandLimitedWaveTable::getTableFor (double phaseDelta) const noexcept
{
    if (phaseDelta <= 0.0)
        return levels.front();

    // Harmonic n is at Nyquist when n * phaseDelta reaches pi, so the harmonics that fit are those
    // below pi / phaseDelta. Capped before converting, since it's huge for very low notes.
    const auto harmonicsThatFit = (int) std::min (std::ceil (juce::MathConstants<double>::pi / phaseDelta) - 1.0,
                                                  (double) getHarmonicLimit (0));

    for (int level = 0; level < numLevels; ++level)
        if (getHarmonicLimit (level) <= harmonicsThatFit)
            return levels[(size_t) level];

    return levels.back();
}

int BandLimitedWaveTable::getHarmonicLimit (int level) noexcept
{
    // A table of `size` samples can hold harmonics below size / 2.
    return level == 0 ? WaveTable::size / 2 - 1 : (WaveTable::size / 2) >> level;
}

} // namespace boringsynth
