#include "TestHelpers.h"

#include "Oscillator.h"

#include <juce_dsp/juce_dsp.h>

// Oscillator tests: the wave tables and the oscillator on their own, compared with std::sin and
// checked for aliasing.

namespace testing
{

namespace
{

// Accuracy limit for a sine read from the table. The theoretical worst case for linear
// interpolation with 2048 samples is about 1.2e-6; this leaves room for float rounding.
constexpr double maxSineError = 2.0e-6;

float sine (double phase) { return (float) std::sin (phase); }

// A sawtooth rising from -1 to 1 over each cycle, with every harmonic. Only used here, to test
// the band-limiting on a waveform that needs it.
float sawtooth (double phase) { return (float) (phase / juce::MathConstants<double>::pi - 1.0); }

const boringsynth::WaveTable& plainSineTable()
{
    static const boringsynth::WaveTable table { sine };
    return table;
}

const boringsynth::BandLimitedWaveTable& bandLimitedSine()
{
    static const boringsynth::BandLimitedWaveTable table { sine };
    return table;
}

const boringsynth::BandLimitedWaveTable& bandLimitedSawtooth()
{
    static const boringsynth::BandLimitedWaveTable table { sawtooth };
    return table;
}

double toDecibels (double ratio)
{
    return 20.0 * std::log10 (ratio);
}

// The magnitude of each harmonic (0 to size / 2) of one cycle stored as `size` samples.
std::vector<double> harmonicsOf (const std::vector<float>& cycle)
{
    const auto size = (int) cycle.size();
    const juce::dsp::FFT fft (juce::roundToInt (std::log2 (size)));
    std::vector<juce::dsp::Complex<float>> in ((size_t) size), out ((size_t) size);

    for (int i = 0; i < size; ++i)
        in[(size_t) i] = cycle[(size_t) i];

    fft.perform (in.data(), out.data(), false);

    std::vector<double> magnitudes ((size_t) size / 2 + 1);

    for (size_t k = 0; k < magnitudes.size(); ++k)
        magnitudes[k] = std::abs (out[k]) / (size / 2.0);

    return magnitudes;
}

// The samples a table holds, read back at their exact positions.
std::vector<float> samplesOf (const boringsynth::WaveTable& table)
{
    std::vector<float> samples ((size_t) boringsynth::WaveTable::size);

    for (int i = 0; i < boringsynth::WaveTable::size; ++i)
        samples[(size_t) i] = table.getSample (juce::MathConstants<double>::twoPi * i / boringsynth::WaveTable::size);

    return samples;
}

//==============================================================================
void testTableMatchesSine()
{
    // Read the table at a million phases spread over the whole cycle, landing between samples
    // in every possible position.
    double largestError = 0.0;

    for (int i = 0; i < 1000000; ++i)
    {
        const auto phase = std::fmod (i * 0.6180339887498949, 1.0) * juce::MathConstants<double>::twoPi;
        largestError = std::max (largestError, std::abs ((double) plainSineTable().getSample (phase) - std::sin (phase)));
    }

    std::printf ("      table vs std::sin: largest error %.2e (%.1f dB)\n", largestError, toDecibels (largestError));
    check (largestError < maxSineError, "the sine table matches std::sin to within 2e-6");
}

void testTableEdges()
{
    check (plainSineTable().getSample (0.0) == 0.0f, "the sine table is exactly 0 at phase 0");
    check (std::abs (plainSineTable().getSample (juce::MathConstants<double>::halfPi) - 1.0f) < 1.0e-6f,
           "the sine table peaks at 1 a quarter of the way through the cycle");

    // The largest phase below 2 pi can round to the very end of the table.
    const auto justUnderTwoPi = std::nextafter (juce::MathConstants<double>::twoPi, 0.0);
    const auto sample = plainSineTable().getSample (justUnderTwoPi);
    check (std::isfinite (sample) && std::abs (sample) < 1.0e-6f, "reading at the very end of the cycle stays inside the table");
}

void testBandLimitedSineLevels()
{
    // A sine has only one harmonic, so band-limiting should leave every level a sine.
    double largestError = 0.0;

    for (int level = 0; level < boringsynth::BandLimitedWaveTable::numLevels; ++level)
    {
        for (int i = 0; i < 100000; ++i)
        {
            const auto phase = std::fmod (i * 0.6180339887498949, 1.0) * juce::MathConstants<double>::twoPi;
            largestError = std::max (largestError,
                                     std::abs ((double) bandLimitedSine().getLevel (level).getSample (phase) - std::sin (phase)));
        }
    }

    std::printf ("      band-limited sine, all %d levels vs std::sin: largest error %.2e (%.1f dB)\n",
                 boringsynth::BandLimitedWaveTable::numLevels, largestError, toDecibels (largestError));
    check (largestError < maxSineError, "every level of the band-limited sine is still a sine");
}

void testBandLimitedHarmonics()
{
    // Each level of a sawtooth should keep its harmonics up to the level's limit, unchanged, and
    // have none above it.
    std::vector<float> original ((size_t) boringsynth::WaveTable::size);

    for (int i = 0; i < boringsynth::WaveTable::size; ++i)
        original[(size_t) i] = sawtooth (juce::MathConstants<double>::twoPi * i / boringsynth::WaveTable::size);

    const auto originalHarmonics = harmonicsOf (original);
    double largestAboveLimit = 0.0, largestChangeBelow = 0.0;

    for (int level = 0; level < boringsynth::BandLimitedWaveTable::numLevels; ++level)
    {
        const auto limit = boringsynth::BandLimitedWaveTable::getHarmonicLimit (level);
        const auto harmonics = harmonicsOf (samplesOf (bandLimitedSawtooth().getLevel (level)));

        for (size_t k = 1; k < harmonics.size(); ++k)
        {
            if ((int) k <= limit)
                largestChangeBelow = std::max (largestChangeBelow, std::abs (harmonics[k] - originalHarmonics[k]));
            else
                largestAboveLimit = std::max (largestAboveLimit, harmonics[k]);
        }
    }

    std::printf ("      sawtooth levels: largest harmonic above a level's limit %.2e (%.1f dB), "
                 "largest change to one below it %.2e\n",
                 largestAboveLimit, toDecibels (largestAboveLimit), largestChangeBelow);
    check (largestAboveLimit < 1.0e-5, "no level keeps a harmonic above its limit");
    check (largestChangeBelow < 1.0e-5, "every level keeps the harmonics below its limit unchanged");
}

void testLevelSelection()
{
    // Across the whole audible range, at two sample rates, the chosen level should have every
    // harmonic below Nyquist, and the next richer level should not.
    const auto& table = bandLimitedSawtooth();
    int wrongChoices = 0, checked = 0;

    for (auto rate : { 44100.0, 48000.0 })
    {
        for (auto frequency = 20.0; frequency < rate / 2.0; frequency *= 1.01)
        {
            const auto phaseDelta = juce::MathConstants<double>::twoPi * frequency / rate;
            const auto& chosen = table.getTableFor (phaseDelta);
            int level = 0;

            while (&table.getLevel (level) != &chosen)
                ++level;

            const auto fits = [phaseDelta] (int limit) { return limit * phaseDelta < juce::MathConstants<double>::pi; };
            const auto richerWouldAlias = level == 0 || ! fits (boringsynth::BandLimitedWaveTable::getHarmonicLimit (level - 1));

            if (! fits (boringsynth::BandLimitedWaveTable::getHarmonicLimit (level)) || ! richerWouldAlias)
                ++wrongChoices;

            ++checked;
        }
    }

    std::printf ("      checked the level chosen at %d pitches: %d wrong\n", checked, wrongChoices);
    check (wrongChoices == 0, "each pitch reads the richest level that doesn't alias");
}

void testNoAliasing()
{
    // A sawtooth at about 5 kHz (48 kHz sample rate) has harmonics at 5, 10, 15 and 20 kHz below
    // Nyquist. Without band-limiting, its higher harmonics fold back to unrelated frequencies.
    //
    // The pitch is chosen so exactly 427 cycles fit in 4096 samples: every real harmonic then lands
    // on a multiple of FFT bin 427, and anything elsewhere in the spectrum is aliasing (or noise).
    constexpr int fftSize = 4096;
    constexpr int cyclesPerFft = 427;
    const auto phaseDelta = juce::MathConstants<double>::twoPi * cyclesPerFft / fftSize;

    auto worstAlias = [] (const std::vector<float>& output)
    {
        const auto harmonics = harmonicsOf (output);
        double worst = 0.0;

        for (size_t k = 1; k < harmonics.size(); ++k)
            if (k % cyclesPerFft != 0)
                worst = std::max (worst, harmonics[k]);

        return toDecibels (worst / harmonics[cyclesPerFft]);
    };

    boringsynth::Oscillator oscillator (bandLimitedSawtooth());
    std::vector<float> bandLimited (fftSize), unlimited (fftSize);
    double phase = 0.0;

    for (size_t i = 0; i < fftSize; ++i)
    {
        bandLimited[i] = oscillator.getNextSample (phaseDelta);

        // The same note read from level 0, which keeps every harmonic, for comparison.
        unlimited[i] = bandLimitedSawtooth().getLevel (0).getSample (phase);
        phase = std::fmod (phase + phaseDelta, juce::MathConstants<double>::twoPi);
    }

    const auto bandLimitedAlias = worstAlias (bandLimited);
    const auto unlimitedAlias = worstAlias (unlimited);
    std::printf ("      5 kHz sawtooth: loudest alias %.1f dB below the fundamental band-limited, "
                 "%.1f dB without band-limiting\n", -bandLimitedAlias, -unlimitedAlias);
    check (bandLimitedAlias < -80.0, "a high sawtooth note has no audible aliasing");
    check (unlimitedAlias > -40.0, "(the same note without band-limiting does alias, so the check above means something)");
}

void testOscillatorMatchesSine()
{
    // Run the oscillator at a range of pitches and follow the same phase by hand with std::sin.
    double largestError = 0.0;

    for (auto frequency : { 20.0, 440.0, 5000.0, 15000.0, 23000.0 })
    {
        boringsynth::Oscillator oscillator (bandLimitedSine());
        const auto phaseDelta = juce::MathConstants<double>::twoPi * frequency / 48000.0;
        double referencePhase = 0.0;

        for (int i = 0; i < 48000; ++i)
        {
            largestError = std::max (largestError, std::abs ((double) oscillator.getNextSample (phaseDelta) - std::sin (referencePhase)));
            referencePhase += phaseDelta;

            if (referencePhase >= juce::MathConstants<double>::twoPi)
                referencePhase -= juce::MathConstants<double>::twoPi;
        }
    }

    std::printf ("      oscillator vs std::sin, 20 Hz to 23 kHz: largest error %.2e (%.1f dB)\n",
                 largestError, toDecibels (largestError));
    check (largestError < maxSineError, "the oscillator matches std::sin to within 2e-6 at every pitch");
}

void testOscillatorAtNyquist()
{
    // At or above Nyquist the oscillator is silent and its phase holds still, so afterwards it
    // carries on exactly where it would have.
    boringsynth::Oscillator steady (bandLimitedSine());
    boringsynth::Oscillator interrupted (bandLimitedSine());

    for (int i = 0; i < 3; ++i)
    {
        steady.getNextSample (0.1);
        interrupted.getNextSample (0.1);
    }

    const auto atNyquist = interrupted.getNextSample (juce::MathConstants<double>::pi);
    const auto aboveNyquist = interrupted.getNextSample (4.0);
    check (atNyquist == 0.0f && aboveNyquist == 0.0f, "the oscillator is silent at and above Nyquist");
    check (steady.getNextSample (0.1) == interrupted.getNextSample (0.1), "its phase holds still while it's silent");
}

} // namespace

void runOscillatorTests()
{
    testTableMatchesSine();
    testTableEdges();
    testBandLimitedSineLevels();
    testBandLimitedHarmonics();
    testLevelSelection();
    testNoAliasing();
    testOscillatorMatchesSine();
    testOscillatorAtNyquist();
}

} // namespace testing
