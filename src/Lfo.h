#pragma once

#include "Waveforms.h"

#include <juce_core/juce_core.h>

namespace boringsynth
{

// The LFO's wave, one sample at a time, in one of the waveforms' shapes (positions in waveformNames).
//
// Every shape runs from exactly -1 to 1 and starts at 0, rising. The sine and triangle are exact. The
// saw's and square's jumps, which would click on the volume or the filter cutoff, become straight
// ramps lasting a fixed number of samples whatever the rate, like the amp envelope's shortest release.
// Elsewhere the square is flat at exactly 1 and -1, and the saw rises in a straight line from -1 to 1.
//
// Like Oscillator, it holds only the phase; the caller passes how far it moves on each sample.
class Lfo
{
public:
    // Starts the wave again from the beginning of its cycle.
    void reset() noexcept { phase = 0.0; }

    // Switches to another shape. With crossfadeSamples of 0 it switches at once; otherwise it fades
    // from the old shape to the new one over that many samples, so a change doesn't jump.
    void setShape (int newShape, int crossfadeSamples) noexcept
    {
        if (newShape == shape)
            return;

        previousShape = crossfadeSamples > 0 ? shape : -1;
        shape = newShape;
        fadeLength = crossfadeSamples;
        fadeRemaining = crossfadeSamples;
    }

    // Returns the next value (-1 to 1) and moves the phase on by phaseDelta radians (2 pi times the
    // rate divided by the sample rate). The saw's and square's jumps take jumpSamples samples. LFO
    // rates are far below the sample rate, so wrapping the phase once is always enough.
    float getNextSample (double phaseDelta, double jumpSamples) noexcept
    {
        const auto jumpWidth = phaseDelta * jumpSamples;
        auto value = getValue (shape, phase, jumpWidth);

        if (previousShape >= 0)
        {
            // Counted in whole samples, so the fade ends on exactly its last sample.
            if (--fadeRemaining <= 0)
            {
                previousShape = -1;
            }
            else
            {
                const auto progress = (float) (fadeLength - fadeRemaining) / (float) fadeLength;
                const auto previousValue = getValue (previousShape, phase, jumpWidth);
                value = previousValue + progress * (value - previousValue);
            }
        }

        phase += phaseDelta;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        return value;
    }

    // A shape's value at a phase from 0 up to 2 pi, with its jumps (if it has any) spread over
    // jumpWidth radians, centred on where the jump would be. jumpWidth must be below pi.
    static float getValue (int shape, double phase, double jumpWidth) noexcept
    {
        constexpr auto pi = juce::MathConstants<double>::pi;

        // From -pi up to pi, so the saw's and square's falling jumps are at either end and their
        // rising ones (the square's) in the middle.
        const auto x = phase < pi ? phase : phase - 2.0 * pi;
        const auto halfJump = 0.5 * jumpWidth;
        const auto distanceFromEnd = pi - std::abs (x);
        const auto sign = x < 0.0 ? -1.0 : 1.0;

        switch (shape)
        {
            case triangleWaveform:
                return (float) (std::abs (x) <= 0.5 * pi ? x / (0.5 * pi) : sign * (2.0 - std::abs (x) / (0.5 * pi)));

            case sawWaveform:
                // Rises from -1 to 1 over the whole cycle except the jump, then falls back.
                if (distanceFromEnd < halfJump)
                    return (float) (sign * distanceFromEnd / halfJump);

                return (float) (x / (pi - halfJump));

            case squareWaveform:
                // Rises through 0 at phase 0 and falls through it at pi, each over jumpWidth.
                if (distanceFromEnd < halfJump)
                    return (float) (sign * distanceFromEnd / halfJump);

                if (std::abs (x) < halfJump)
                    return (float) (x / halfJump);

                return (float) sign;

            default:
                return (float) std::sin (phase);
        }
    }

private:
    int shape = sineWaveform;
    int previousShape = -1; // the shape being faded out, if any
    int fadeLength = 0;     // samples the current crossfade lasts
    int fadeRemaining = 0;  // samples of it still to go
    double phase = 0.0;
};

} // namespace boringsynth
