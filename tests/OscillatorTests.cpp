#include "TestHelpers.h"

#include "Oscillator.h"

// Oscillator tests: the wave table and the oscillator on their own, compared with std::sin.

namespace testing
{

namespace
{

// Accuracy limit for a sine read from the table. The theoretical worst case for linear
// interpolation with 2048 samples is about 1.2e-6; this leaves room for float rounding.
constexpr double maxSineError = 2.0e-6;

const boringsynth::WaveTable& sineTable()
{
    static const boringsynth::WaveTable table { [] (double phase) { return (float) std::sin (phase); } };
    return table;
}

double toDecibels (double error)
{
    return 20.0 * std::log10 (error);
}

void testTableMatchesSine()
{
    // Read the table at a million phases spread over the whole cycle, landing between samples
    // in every possible position.
    double largestError = 0.0;

    for (int i = 0; i < 1000000; ++i)
    {
        const auto phase = std::fmod (i * 0.6180339887498949, 1.0) * juce::MathConstants<double>::twoPi;
        largestError = std::max (largestError, std::abs ((double) sineTable().getSample (phase) - std::sin (phase)));
    }

    std::printf ("      table vs std::sin: largest error %.2e (%.1f dB)\n", largestError, toDecibels (largestError));
    check (largestError < maxSineError, "the sine table matches std::sin to within 2e-6");
}

void testTableEdges()
{
    check (sineTable().getSample (0.0) == 0.0f, "the sine table is exactly 0 at phase 0");
    check (std::abs (sineTable().getSample (juce::MathConstants<double>::halfPi) - 1.0f) < 1.0e-6f,
           "the sine table peaks at 1 a quarter of the way through the cycle");

    // The largest phase below 2 pi can round to the very end of the table.
    const auto justUnderTwoPi = std::nextafter (juce::MathConstants<double>::twoPi, 0.0);
    const auto sample = sineTable().getSample (justUnderTwoPi);
    check (std::isfinite (sample) && std::abs (sample) < 1.0e-6f, "reading at the very end of the cycle stays inside the table");
}

void testOscillatorMatchesSine()
{
    // Run the oscillator at a range of pitches and follow the same phase by hand with std::sin.
    double largestError = 0.0;

    for (auto frequency : { 20.0, 440.0, 5000.0, 15000.0, 23000.0 })
    {
        boringsynth::Oscillator oscillator (sineTable());
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
    boringsynth::Oscillator steady (sineTable());
    boringsynth::Oscillator interrupted (sineTable());

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
    testOscillatorMatchesSine();
    testOscillatorAtNyquist();
}

} // namespace testing
