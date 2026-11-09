#include "WavetableLibrary.h"

#include "Waveforms.h"

namespace boringsynth
{

namespace
{

constexpr auto pi = juce::MathConstants<double>::pi;

// A table of numFrames frames. shape gives a frame's value at a phase, for how far the frame is through
// the table, from 0 to 1. Each frame is then scaled to the sine's RMS, 1 / sqrt (2).
MultiFrameWaveTable makeTable (int numFrames, const std::function<double (double amount, double phase)>& shape)
{
    constexpr auto size = WaveTable::size;
    std::vector<std::vector<float>> cycles ((size_t) numFrames, std::vector<float> ((size_t) size));

    for (int frame = 0; frame < numFrames; ++frame)
    {
        const auto amount = (double) frame / (numFrames - 1);
        std::vector<double> values ((size_t) size);
        double sumOfSquares = 0.0;

        for (int i = 0; i < size; ++i)
        {
            values[(size_t) i] = shape (amount, juce::MathConstants<double>::twoPi * i / size);
            sumOfSquares += values[(size_t) i] * values[(size_t) i];
        }

        const auto gain = std::sqrt (0.5 / (sumOfSquares / size));

        for (int i = 0; i < size; ++i)
            cycles[(size_t) frame][(size_t) i] = (float) (gain * values[(size_t) i]);
    }

    return MultiFrameWaveTable (cycles);
}

// The phase measured from the middle of the cycle's start, from -pi up to pi: a function that's odd in
// this is zero at phase 0. A jump at pi lands exactly on a sample there, which takes the jump's midpoint,
// 0, as the waveforms' jumps do.
double centredPhase (double phase)
{
    if (phase == pi)
        return 0.0;

    return phase < pi ? phase : phase - 2.0 * pi;
}

// A straight blend from a sine to the saw, the two matched in loudness.
double sineToSaw (double amount, double phase)
{
    const auto sine = (double) waveformShape (sineWaveform, phase);
    const auto saw = (double) (waveformGain (sawWaveform) * waveformShape (sawWaveform, phase));
    return sine + amount * (saw - sine);
}

// A pulse in each half of the cycle, up then down, from a square at 0 down to an eighth of that wide at
// 1: hollow, then thinner and more nasal.
double pulseWidth (double amount, double phase)
{
    const auto x = centredPhase (phase);
    const auto halfCycleWidth = pi * (1.0 - amount * 7.0 / 8.0);

    if (x == 0.0 || std::abs (x) >= halfCycleWidth)
        return 0.0;

    return x > 0.0 ? 1.0 : -1.0;
}

// A sine playing up to 8 cycles in the time of one, then restarting: hard sync, as if a second
// oscillator kept resetting it. A plain sine at 0, with a brighter, tearing resonance as it rises.
double hardSync (double amount, double phase)
{
    return std::sin ((1.0 + amount * 7.0) * centredPhase (phase));
}

// Frequency modulation frozen into a wave: a sine whose phase is pushed by another sine, of the same
// frequency, up to an index of 6. A plain sine at 0, brighter and more metallic as it rises.
double fmSweep (double amount, double phase)
{
    return std::sin (phase + amount * 6.0 * std::sin (phase));
}

// The same with the modulator an octave up, which leaves only odd harmonics: hollow and woody.
double fmHollow (double amount, double phase)
{
    return std::sin (phase + amount * 6.0 * std::sin (2.0 * phase));
}

// A sine driven harder and harder through a sine-shaped curve, which folds its peaks back on themselves.
// Nearly a plain sine at 0, then more and more folds.
double wavefold (double amount, double phase)
{
    return std::sin ((0.1 + amount * 11.9) * std::sin (phase));
}

// A hump of harmonics, as a vowel or a resonant body has, sliding up from around the fundamental to
// around harmonic 12. Pairs of harmonics change sign, as all of them in step would add up to one sharp
// spike, far louder at its peak than it sounds.
double formant (double amount, double phase)
{
    const auto centre = amount * std::log2 (12.0);
    double value = 0.0;

    for (int harmonic = 1; harmonic <= 48; ++harmonic)
    {
        const auto octavesFromCentre = std::log2 ((double) harmonic) - centre;
        const auto strength = std::exp (-octavesFromCentre * octavesFromCentre / (2.0 * 0.5 * 0.5));
        value += ((harmonic - 1) % 4 < 2 ? strength : -strength) * std::sin (harmonic * phase);
    }

    return value;
}

// One harmonic at a time, from the fundamental to harmonic 8, each fading into the next at equal power.
double harmonics (double amount, double phase)
{
    const auto place = amount * 7.0;
    const auto lower = std::min ((int) place, 6);
    const auto fade = place - lower;
    return std::sqrt (1.0 - fade) * std::sin ((lower + 1) * phase) + std::sqrt (fade) * std::sin ((lower + 2) * phase);
}

} // namespace

std::vector<MultiFrameWaveTable> makeWavetables()
{
    std::vector<MultiFrameWaveTable> tables;
    tables.push_back (makeTable (8, sineToSaw));
    tables.push_back (makeTable (16, pulseWidth));
    tables.push_back (makeTable (32, hardSync));
    tables.push_back (makeTable (16, fmSweep));
    tables.push_back (makeTable (16, fmHollow));
    tables.push_back (makeTable (32, wavefold));
    tables.push_back (makeTable (16, formant));
    tables.push_back (makeTable (32, harmonics));
    return tables;
}

} // namespace boringsynth
