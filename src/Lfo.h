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
//
// A change of shape, or a restart, can fade from the wave as it was (the "outgoing" wave, which
// carries on in its old shape from its old phase) to the new one, so nothing jumps.
class Lfo
{
public:
    // Starts the wave again from the beginning of its cycle at once, with no fade.
    void reset() noexcept
    {
        phase = 0.0;
        fadeRemaining = 0;
    }

    // Starts the wave again from the beginning of its cycle, fading from where it had got to over
    // crossfadeSamples samples.
    void restart (int crossfadeSamples) noexcept
    {
        startFade (crossfadeSamples);
        phase = 0.0;
    }

    // Switches to another shape. With crossfadeSamples of 0 it switches at once; otherwise it fades
    // from the old shape to the new one over that many samples, so a change doesn't jump.
    void setShape (int newShape, int crossfadeSamples) noexcept
    {
        if (newShape == shape)
            return;

        startFade (crossfadeSamples);
        shape = newShape;
    }

    // Returns the next value (-1 to 1) and moves the phase on by phaseDelta radians (2 pi times the
    // rate divided by the sample rate). The saw's and square's jumps take jumpSamples samples. LFO
    // rates are far below the sample rate, so wrapping the phase once is always enough.
    float getNextSample (double phaseDelta, double jumpSamples) noexcept
    {
        const auto jumpWidth = phaseDelta * jumpSamples;
        auto value = getValue (shape, phase, jumpWidth);

        if (fadeRemaining > 0)
        {
            // Counted in whole samples, so the fade ends on exactly its last sample.
            if (--fadeRemaining > 0)
            {
                const auto progress = (float) (fadeLength - fadeRemaining) / (float) fadeLength;
                const auto outgoingValue = getValue (outgoingShape, outgoingPhase, jumpWidth);
                value = outgoingValue + progress * (value - outgoingValue);
            }

            advance (outgoingPhase, phaseDelta);
        }

        advance (phase, phaseDelta);
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
    double phase = 0.0;

    // The wave being faded out, while fadeRemaining is above 0.
    int outgoingShape = sineWaveform;
    double outgoingPhase = 0.0;
    int fadeLength = 1;    // samples the current crossfade lasts; never 0, as the fade divides by it
    int fadeRemaining = 0; // samples of it still to go

    // Begins fading out the wave as it is now. If a fade is already under way, it keeps fading from
    // the same outgoing wave, starting over: so a chord, whose notes each restart the LFO on the same
    // sample, fades once from where the LFO was, rather than cutting the first fade short.
    void startFade (int crossfadeSamples) noexcept
    {
        if (crossfadeSamples <= 0)
        {
            fadeRemaining = 0;
            return;
        }

        if (fadeRemaining <= 0)
        {
            outgoingShape = shape;
            outgoingPhase = phase;
        }

        fadeLength = crossfadeSamples;
        fadeRemaining = crossfadeSamples;
    }

    static void advance (double& wavePhase, double phaseDelta) noexcept
    {
        wavePhase += phaseDelta;

        if (wavePhase >= juce::MathConstants<double>::twoPi)
            wavePhase -= juce::MathConstants<double>::twoPi;
    }
};

} // namespace boringsynth
