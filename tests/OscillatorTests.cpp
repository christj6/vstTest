#include "TestHelpers.h"

#include "Oscillator.h"
#include "Waveforms.h"

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

// For the aliasing tests: a note at about 5 kHz (48 kHz sample rate), whose harmonics below Nyquist
// are at 5, 10, 15 and 20 kHz. Without band-limiting, its higher harmonics fold back to unrelated
// frequencies.
//
// The pitch is chosen so exactly 427 cycles fit in 4096 samples: every real harmonic then lands on
// a multiple of FFT bin 427, and anything elsewhere in the spectrum is aliasing (or noise).
constexpr int aliasTestLength = 4096;
constexpr int aliasTestCycles = 427;
const double aliasTestPhaseDelta = juce::MathConstants<double>::twoPi * aliasTestCycles / aliasTestLength;

// The loudest frequency in `output` that isn't a harmonic of the note, in dB relative to the note.
double loudestAlias (const std::vector<float>& output)
{
    const auto harmonics = harmonicsOf (output);
    double loudest = 0.0;

    for (size_t k = 1; k < harmonics.size(); ++k)
        if (k % aliasTestCycles != 0)
            loudest = std::max (loudest, harmonics[k]);

    return toDecibels (loudest / harmonics[aliasTestCycles]);
}

// The aliasing test note played by an oscillator.
std::vector<float> playAliasTestNote (const boringsynth::BandLimitedWaveTable& table)
{
    boringsynth::Oscillator oscillator (table);
    std::vector<float> output (aliasTestLength);

    for (auto& sample : output)
        sample = oscillator.getNextSample (aliasTestPhaseDelta);

    return output;
}

void testNoAliasing()
{
    // The same note read from level 0, which keeps every harmonic, for comparison.
    std::vector<float> unlimited (aliasTestLength);
    double phase = 0.0;

    for (auto& sample : unlimited)
    {
        sample = bandLimitedSawtooth().getLevel (0).getSample (phase);
        phase = std::fmod (phase + aliasTestPhaseDelta, juce::MathConstants<double>::twoPi);
    }

    const auto bandLimitedAlias = loudestAlias (playAliasTestNote (bandLimitedSawtooth()));
    const auto unlimitedAlias = loudestAlias (unlimited);
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

//==============================================================================
const boringsynth::WaveformTables& waveformTables()
{
    static const boringsynth::WaveformTables tables;
    return tables;
}

const char* waveformName (int waveform)
{
    return boringsynth::waveformNames[waveform].toRawUTF8();
}

double rmsOf (const std::vector<float>& samples)
{
    double sumOfSquares = 0.0;

    for (auto s : samples)
        sumOfSquares += (double) s * s;

    return std::sqrt (sumOfSquares / (double) samples.size());
}

void testWaveformLoudness()
{
    // Every waveform is scaled to the sine's RMS, 1 / sqrt (2).
    const auto sineRms = 1.0 / std::sqrt (2.0);
    double worstDifference = 0.0;

    for (int waveform = 0; waveform < boringsynth::waveformNames.size(); ++waveform)
    {
        const auto rms = rmsOf (samplesOf (waveformTables().get (waveform).getLevel (0)));
        std::printf ("      %-8s RMS %.4f (%+.2f dB from the sine)\n", waveformName (waveform), rms, toDecibels (rms / sineRms));
        worstDifference = std::max (worstDifference, std::abs (toDecibels (rms / sineRms)));
    }

    check (worstDifference < 0.1, "every waveform is within 0.1 dB of the sine's loudness (RMS)");
}

void testWaveformsStartAtZero()
{
    // Notes start at phase 0, so a waveform that isn't zero there would click at every note on.
    double largest = 0.0;

    for (int waveform = 0; waveform < boringsynth::waveformNames.size(); ++waveform)
        for (int level = 0; level < boringsynth::BandLimitedWaveTable::numLevels; ++level)
            largest = std::max (largest, (double) std::abs (waveformTables().get (waveform).getLevel (level).getSample (0.0)));

    std::printf ("      largest value at phase 0, any waveform and level: %.2e\n", largest);
    check (largest < 1.0e-5, "every waveform starts at zero, so notes start without a click");
}

void testWaveformShapes()
{
    // Each waveform's richest level should match its ideal shape, except right next to a jump,
    // where band-limiting necessarily rounds the corner and rings a little.
    double worst = 0.0;

    for (int waveform = 0; waveform < boringsynth::waveformNames.size(); ++waveform)
    {
        double largestError = 0.0;

        for (int i = 0; i < 10000; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * i / 10000.0;
            const auto cyclePosition = i / 10000.0;
            const auto nearJump = (waveform == 2 && std::abs (cyclePosition - 0.5) < 0.02)
                               || (waveform == 3 && (cyclePosition < 0.02 || std::abs (cyclePosition - 0.5) < 0.02 || cyclePosition > 0.98));

            if (nearJump)
                continue;

            const auto ideal = boringsynth::waveformGain (waveform) * boringsynth::waveformShape (waveform, phase);
            largestError = std::max (largestError, (double) std::abs (waveformTables().get (waveform).getLevel (0).getSample (phase) - ideal));
        }

        std::printf ("      %-8s largest difference from its ideal shape %.4f\n", waveformName (waveform), largestError);
        worst = std::max (worst, largestError);
    }

    check (worst < 0.02, "every waveform has the shape its name says (away from jumps)");
}

void testWaveformsDontAlias()
{
    double worst = -1000.0;

    for (int waveform = 0; waveform < boringsynth::waveformNames.size(); ++waveform)
    {
        const auto alias = loudestAlias (playAliasTestNote (waveformTables().get (waveform)));
        std::printf ("      %-8s at 5 kHz: loudest alias %.1f dB below the fundamental\n", waveformName (waveform), -alias);
        worst = std::max (worst, alias);
    }

    check (worst < -80.0, "no waveform has audible aliasing at a high note");
}

void testCrossfadeBetweenTables()
{
    // While switching tables, the output should blend from the old waveform to the new one, reading
    // both at the same phase, then be exactly the new waveform.
    const auto& saw = waveformTables().get (2);
    const auto& square = waveformTables().get (3);
    boringsynth::Oscillator fading (saw), sawOnly (saw), squareOnly (square);
    constexpr int fadeLength = 100;

    for (int i = 0; i < 50; ++i)
    {
        fading.getNextSample (0.05);
        sawOnly.getNextSample (0.05);
        squareOnly.getNextSample (0.05);
    }

    fading.setTable (square, fadeLength);
    double largestBlendError = 0.0;
    bool exactAfterFade = true;

    for (int i = 1; i <= 2 * fadeLength; ++i)
    {
        const auto out = fading.getNextSample (0.05);
        const auto from = sawOnly.getNextSample (0.05);
        const auto to = squareOnly.getNextSample (0.05);

        if (i < fadeLength)
            largestBlendError = std::max (largestBlendError, (double) std::abs (out - (from + (float) i / fadeLength * (to - from))));
        else
            exactAfterFade = exactAfterFade && out == to;
    }

    check (largestBlendError < 1.0e-6, "switching waveform blends the two over the crossfade");
    check (exactAfterFade, "after the crossfade the output is exactly the new waveform");
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
    testWaveformLoudness();
    testWaveformsStartAtZero();
    testWaveformShapes();
    testWaveformsDontAlias();
    testCrossfadeBetweenTables();
}

} // namespace testing
