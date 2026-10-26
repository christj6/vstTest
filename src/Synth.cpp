#include "Synth.h"

#include "Parameters.h"

namespace boringsynth
{

namespace
{

// How long the LFO's depth takes to follow its knob, so turning it doesn't make the pitch jump.
constexpr double lfoDepthSmoothingSeconds = 0.02;

// How long a change of LFO destination takes to fade the LFO out of the old one and into the new.
constexpr double lfoDestinationSwitchSeconds = 0.01;

// How long a change of LFO shape takes to fade from the old shape to the new one.
constexpr double lfoShapeCrossfadeSeconds = 0.01;

// MIDI controller numbers.
constexpr int modWheelController = 1;
constexpr int resetAllControllers = 121;

// How far, as a fraction of its cycle, a synced LFO may drift from the song's position before it's
// moved back (with a fade). It never drifts that far in ordinary playback, so only loops, jumps and
// starting playback move it, and the output doesn't depend on the host's block size.
constexpr double lfoSyncTolerance = 1.0e-4;

// How far the LFO moves the pitch, in semitones either way, for a depth from 0 to 1. Squared, so
// most of the knob goes to the small depths vibrato uses: 20 % is about half a semitone, and 100 %
// is the full octave. At a depth of 0 it's exactly 0.
float lfoPitchSemitones (float depth) noexcept
{
    return maxLfoPitchSemitones * depth * depth;
}

// The LFO's effect on a note's level, for a depth from 0 to 1 and the LFO's value from -1 to 1:
// full level at the LFO's peak, down to 1 - depth at its trough. Never more than 1, so the LFO can't
// push the output past full scale. At a depth of 0 it's exactly 1.
float lfoGain (float depth, float lfoValue) noexcept
{
    return 1.0f - depth * (1.0f - lfoValue) * 0.5f;
}

} // namespace

Synth::Synth (const LfoParameters& lfoParameters)
    : parameters (lfoParameters)
{
}

void Synth::setCurrentPlaybackSampleRate (double newRate)
{
    Synthesiser::setCurrentPlaybackSampleRate (newRate);

    // The LFO starts again from the beginning of its cycle, in its current shape and at the current
    // depth rather than fading or gliding to them.
    lfo.reset();
    lfo.setShape (getCurrentShape(), 0);
    lfoDepth.reset (newRate, lfoDepthSmoothingSeconds);
    lfoDepth.setCurrentAndTargetValue (getLfoDepthTarget());

    const auto destination = getCurrentDestination();

    for (int i = 0; i < (int) destinationAmounts.size(); ++i)
    {
        destinationAmounts[(size_t) i].reset (newRate, lfoDestinationSwitchSeconds);
        destinationAmounts[(size_t) i].setCurrentAndTargetValue (i == destination ? 1.0f : 0.0f);
    }
}

void Synth::renderVoices (juce::AudioBuffer<float>& outputAudio, int startSample, int numSamples)
{
    const auto currentSampleRate = getSampleRate();
    const auto lfoPhaseDelta = currentSampleRate > 0.0 ? juce::MathConstants<double>::twoPi * getLfoRateHz() / currentSampleRate : 0.0;
    const auto lfoJumpSamples = lfoJumpSeconds * currentSampleRate;
    // Picks up moves of the depth knob and the mod wheel; either way the depth glides.
    lfoDepth.setTargetValue (getLfoDepthTarget());

    // A change of shape fades from the old one to the new one.
    lfo.setShape (getCurrentShape(), juce::roundToInt (lfoShapeCrossfadeSeconds * currentSampleRate));

    // A change of destination crossfades from the old one to the new one.
    const auto destination = getCurrentDestination();

    for (int i = 0; i < (int) destinationAmounts.size(); ++i)
        destinationAmounts[(size_t) i].setTargetValue (i == destination ? 1.0f : 0.0f);

    // The LFO moves on one sample at a time, in order, so the voices hear the same LFO whatever
    // size of block the host sends.
    while (numSamples > 0)
    {
        const auto numThisTime = std::min (numSamples, Modulation::maxSamples);
        modulation.startSample = startSample;

        for (int i = 0; i < numThisTime; ++i)
        {
            const auto depth = lfoDepth.getNextValue();
            const auto lfoValue = lfo.getNextSample (lfoPhaseDelta, lfoJumpSamples);
            const auto pitchDepth = depth * destinationAmounts[(size_t) pitchDestination].getNextValue();
            const auto volumeDepth = depth * destinationAmounts[(size_t) volumeDestination].getNextValue();
            const auto cutoffDepth = depth * destinationAmounts[(size_t) cutoffDestination].getNextValue();

            modulation.pitchRatios[(size_t) i] = std::exp2 (lfoPitchSemitones (pitchDepth) * lfoValue / 12.0f);
            modulation.gains[(size_t) i] = lfoGain (volumeDepth, lfoValue);
            modulation.cutoffOctaves[(size_t) i] = maxLfoCutoffOctaves * cutoffDepth * lfoValue;
            modulation.cutoffMoving[(size_t) i] = cutoffDepth != 0.0f;
        }

        Synthesiser::renderVoices (outputAudio, startSample, numThisTime);
        startSample += numThisTime;
        numSamples -= numThisTime;
    }
}

void Synth::noteOn (int midiChannel, int midiNoteNumber, float velocity)
{
    // With retrigger on, every note starts the LFO from the beginning of its cycle. It fades from
    // where it had got to over the same 5 ms as the saw's and square's jumps, so notes already
    // sounding don't jump.
    if (parameters.retrigger.load() >= 0.5f)
        lfo.restart (juce::roundToInt (lfoJumpSeconds * getSampleRate()));

    Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
}

void Synth::handleController (int midiChannel, int controllerNumber, int controllerValue)
{
    // MIDI's mod wheel, controller 1 (its fine-resolution half, controller 33, is ignored), and
    // "reset all controllers", 121, which puts it back down.
    if (controllerNumber == modWheelController)
        modWheel = (float) controllerValue / 127.0f;
    else if (controllerNumber == resetAllControllers)
        modWheel = 0.0f;

    Synthesiser::handleController (midiChannel, controllerNumber, controllerValue);
}

void Synth::setTransport (const Transport& transport)
{
    if (transport.tempoBpm.has_value() && *transport.tempoBpm > 0.0)
        tempoBpm = *transport.tempoBpm;

    // Synced, and while the song plays, the LFO's position follows the song's: one cycle every so many
    // quarter notes, counted from the start of the song. Unless notes retrigger it, which wins. If it
    // has drifted (which only a loop, a jump or starting playback does), it fades to where it should be.
    const auto followSong = isSynced() && parameters.retrigger.load() < 0.5f
                         && transport.isPlaying && transport.songPositionQuarterNotes.has_value();

    if (! followSong)
        return;

    constexpr auto twoPi = juce::MathConstants<double>::twoPi;
    const auto cycles = *transport.songPositionQuarterNotes / getSyncedCycleQuarterNotes();
    const auto songPhase = twoPi * (cycles - std::floor (cycles));
    const auto drift = std::remainder (songPhase - lfo.getPhase(), twoPi) / twoPi;

    if (std::abs (drift) > lfoSyncTolerance)
        lfo.restart (juce::roundToInt (lfoJumpSeconds * getSampleRate()), songPhase < twoPi ? songPhase : 0.0);
}

// The LFO's rate in Hz: the knob's, or synced, a cycle every so many quarter notes at the tempo, at
// most maxSyncedLfoRateHz.
double Synth::getLfoRateHz() const
{
    if (! isSynced())
        return parameters.rate.load();

    return std::min (tempoBpm / 60.0 / getSyncedCycleQuarterNotes(), maxSyncedLfoRateHz);
}

double Synth::getSyncedCycleQuarterNotes() const
{
    return lfoSyncRateQuarterNotes[juce::jlimit (0, lfoSyncRateNames.size() - 1, juce::roundToInt (parameters.syncRate.load()))];
}

bool Synth::isSynced() const
{
    return parameters.sync.load() >= 0.5f;
}

// The LFO's depth, 0 to 1: the knob, plus the mod wheel scaled by how much it adds, at most 1. With
// the wheel's amount at 0 it's exactly the knob.
float Synth::getLfoDepthTarget() const
{
    return std::min (1.0f, parameters.depth.load() / 100.0f + parameters.modWheelDepth.load() / 100.0f * modWheel);
}

int Synth::getCurrentShape() const
{
    return juce::jlimit (0, waveformNames.size() - 1, juce::roundToInt (parameters.shape.load()));
}

int Synth::getCurrentDestination() const
{
    return juce::jlimit (0, lfoDestinationNames.size() - 1, juce::roundToInt (parameters.destination.load()));
}

} // namespace boringsynth
