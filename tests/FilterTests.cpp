#include "TestHelpers.h"

#include "Filter.h"

// Filter tests: the filter on its own, at 48 kHz, against the textbook response of a two-pole
// low-pass filter.

namespace testing
{

namespace
{

constexpr double filterTestRate = 48000.0;

boringsynth::Filter makeFilter (float cutoffHz, float q, double rate = filterTestRate)
{
    boringsynth::Filter filter;
    filter.setSampleRate (rate);
    filter.setCutoffAndQ (cutoffHz, q);
    return filter;
}

// The filter's gain at one frequency, in dB: a sine through it, measured once it has settled.
double gainAt (float cutoffHz, float q, double frequency)
{
    auto filter = makeFilter (cutoffHz, q);
    double inputSquares = 0.0, outputSquares = 0.0;

    for (int i = 0; i < 48000; ++i)
    {
        const auto input = (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / filterTestRate);
        const auto output = filter.processLowPass (input);

        if (i >= 24000)
        {
            inputSquares += (double) input * input;
            outputSquares += (double) output * output;
        }
    }

    return 10.0 * std::log10 (outputSquares / inputSquares);
}

//==============================================================================
void testLowPassResponse()
{
    // With no resonance, a two-pole low-pass filter passes low frequencies, is 3 dB down at its
    // cutoff, and falls 12 dB per octave above it: about -12.3 dB an octave up, -24.1 dB two up.
    const auto q = boringsynth::Filter::minQ;
    const auto below = gainAt (1000.0f, q, 100.0);
    const auto atCutoff = gainAt (1000.0f, q, 1000.0);
    const auto octaveUp = gainAt (1000.0f, q, 2000.0);
    const auto twoOctavesUp = gainAt (1000.0f, q, 4000.0);
    std::printf ("      1 kHz cutoff: %.2f dB at 100 Hz, %.2f at 1 kHz, %.2f at 2 kHz, %.2f at 4 kHz "
                 "(expected 0, -3.01, -12.30, -24.10)\n", below, atCutoff, octaveUp, twoOctavesUp);
    check (std::abs (below) < 0.05 && std::abs (atCutoff + 3.01) < 0.1, "the filter passes low frequencies and is 3 dB down at its cutoff");
    check (std::abs (octaveUp + 12.30) < 0.3 && std::abs (twoOctavesUp + 24.10) < 0.6, "above the cutoff it falls 12 dB per octave");
}

void testResonancePeak()
{
    // Full resonance is a Q of 10: a peak of +20 dB at the cutoff.
    const auto q = boringsynth::Filter::resonanceToQ (1.0f);
    const auto peak = gainAt (1000.0f, q, 1000.0);
    std::printf ("      full resonance: Q %.2f, %.2f dB at the cutoff (expected +20)\n", q, peak);
    check (std::abs (boringsynth::Filter::resonanceToQ (0.0f) - boringsynth::Filter::minQ) < 1.0e-6f && std::abs (q - 10.0f) < 1.0e-4f,
           "resonance 0 is a flat response (Q 0.707) and 100 % is Q 10");
    check (std::abs (peak - 20.0) < 0.3, "full resonance peaks by 20 dB at the cutoff");
}

void testStableUnderModulation()
{
    // Full resonance, the cutoff jumping anywhere between 20 Hz and 20 kHz on every sample, and loud
    // random input: the output should stay finite and bounded.
    auto filter = makeFilter (1000.0f, boringsynth::Filter::maxQ);
    juce::Random random (1234);
    bool allFinite = true;
    float largest = 0.0f;

    for (int i = 0; i < 480000; ++i)
    {
        filter.setCutoffAndQ (20.0f * std::pow (1000.0f, random.nextFloat()), boringsynth::Filter::maxQ);
        const auto output = filter.processLowPass (random.nextFloat() * 2.0f - 1.0f);
        allFinite = allFinite && std::isfinite (output);
        largest = std::max (largest, std::abs (output));
    }

    std::printf ("      10 s of noise with the cutoff jumping every sample at full resonance: largest output %.2f\n", largest);
    check (allFinite && largest < 100.0f, "the filter stays stable however fast its cutoff changes");
}

void testCutoffAboveNyquist()
{
    // A 20 kHz cutoff at a 16 kHz sample rate is above Nyquist; it should be capped, not break.
    auto filter = makeFilter (20000.0f, boringsynth::Filter::minQ, 16000.0);
    bool allFinite = true;

    for (int i = 0; i < 16000; ++i)
        allFinite = allFinite && std::isfinite (filter.processLowPass ((float) std::sin (i * 0.3)));

    check (allFinite, "a cutoff above the Nyquist frequency is capped instead of breaking the filter");
}

void testCopyCarriesOn()
{
    // A copy taken mid-sound (as a stolen voice's fading note does) carries on exactly as the
    // original would have.
    auto original = makeFilter (500.0f, 4.0f);

    for (int i = 0; i < 1000; ++i)
        original.processLowPass ((float) std::sin (i * 0.05));

    auto copy = original;
    bool identical = true;

    for (int i = 1000; i < 2000; ++i)
        identical = identical && original.processLowPass ((float) std::sin (i * 0.05)) == copy.processLowPass ((float) std::sin (i * 0.05));

    check (identical, "a copy of the filter carries on exactly where the original was");
}

void testReset()
{
    // After a reset, the filter responds as if it had never heard anything.
    auto used = makeFilter (300.0f, 8.0f);
    auto fresh = makeFilter (300.0f, 8.0f);

    for (int i = 0; i < 1000; ++i)
        used.processLowPass (1.0f);

    used.reset();
    bool identical = true;

    for (int i = 0; i < 1000; ++i)
        identical = identical && used.processLowPass ((float) std::cos (i * 0.1)) == fresh.processLowPass ((float) std::cos (i * 0.1));

    check (identical, "after a reset the filter starts fresh");
}

} // namespace

void runFilterTests()
{
    testLowPassResponse();
    testResonancePeak();
    testStableUnderModulation();
    testCutoffAboveNyquist();
    testCopyCarriesOn();
    testReset();
}

} // namespace testing
