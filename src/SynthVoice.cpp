#include "SynthVoice.h"

namespace boringsynth
{

namespace
{

// Fixed output level per note. Eight notes peaking together reach 0.8, below full scale.
constexpr float level = 0.1f;

// How long the pitch takes to follow the wheel. Many keyboards send the wheel in coarse steps;
// this smooths them out while still feeling immediate.
constexpr double bendSmoothingSeconds = 0.01;

// How long a change of waveform during a note takes to fade from the old waveform to the new one.
constexpr double waveformCrossfadeSeconds = 0.01;

// How long a note that's cut off (for instance, when its voice is stolen) takes to fade out. The
// same as the shortest release, which is short enough to sound instant and long enough not to click.
constexpr double cutOffFadeSeconds = 0.005;

// How long the filter's cutoff and resonance take to follow their knobs, so moving them (or
// automating them) doesn't make a crackling "zipper" noise. The cutoff glides evenly in pitch.
constexpr double filterSmoothingSeconds = 0.02;

// How long switching the filter on or off takes to crossfade between the two sounds.
constexpr double filterSwitchSeconds = 0.01;

// While the cutoff or resonance glides, the filter's coefficients are worked out again every this
// many samples rather than every sample. That takes a tangent and a power, so this cuts the cost of
// a moving filter several times over; 8 samples is under 0.2 ms, far too short to hear as steps.
constexpr int filterUpdateInterval = 8;

// The note at which key tracking leaves the cutoff exactly where the knob says: middle C. With key
// tracking, notes above it open the filter further and notes below close it.
constexpr int keyTrackingReferenceNote = 60;

// A note's level for its velocity (0 to 1) and the velocity sensitivity (0 to 1). At full
// sensitivity this is the General MIDI curve, velocity squared, where each halving of velocity is
// about -12 dB. At zero sensitivity velocity is ignored. The hardest hit is always full level.
float velocityToGain (float velocity, float sensitivity)
{
    return (1.0f - sensitivity) + sensitivity * velocity * velocity;
}

// Converts the MIDI pitch wheel (0 to 16383, centre 8192) to -1 (fully down) to 1 (fully up).
// The two halves are scaled separately because the wheel has one more step below the centre than
// above it, and both extremes should reach the full bend range.
float pitchWheelToBend (int wheelValue)
{
    const auto offset = (float) (wheelValue - 8192);
    return offset >= 0.0f ? offset / 8191.0f : offset / 8192.0f;
}

// The filter's output for the chosen type, given how much of each type is heard (indexed by
// position in filterTypeNames). Exactly the chosen type's output when its amount is 1 and the
// others are 0; a blend while crossfading between types.
float blendFilterTypes (const Filter::Outputs& outputs, float lowPass, float highPass, float bandPass) noexcept
{
    return lowPass * outputs.lowPass + highPass * outputs.highPass + bandPass * outputs.bandPass;
}

// A cutoff moved by the filter envelope: up by amountOctaves when the envelope is at its peak (1),
// not at all when it's at 0, and kept within the cutoff's range. With an amount of 0 it's exactly
// the cutoff given.
float envelopedCutoff (float cutoffHz, float amountOctaves, float envelopeLevel) noexcept
{
    return std::clamp (cutoffHz * std::exp2 (amountOctaves * envelopeLevel), minCutoffHz, maxCutoffHz);
}

} // namespace

SynthVoice::SynthVoice (const WaveformTables& waveformTables, const VoiceParameters& voiceParameters)
    : tables (waveformTables),
      parameters (voiceParameters),
      oscillator (waveformTables.get (0))
{
}

bool SynthVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<SynthSound*> (sound) != nullptr;
}

void SynthVoice::startNote (int midiNote, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition)
{
    const auto sampleRate = getSampleRate();

    // Stay silent if the host hasn't given us a sample rate yet.
    if (sampleRate <= 0.0)
    {
        clearCurrentNote();
        phaseDelta = 0.0;
        return;
    }

    oscillator.reset();
    oscillator.setTable (getCurrentTable(), 0);
    phaseDelta = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (midiNote) / sampleRate;
    noteLevel = level * velocityToGain (velocity, parameters.velocitySensitivity.load() / 100.0f);
    noteNumber = midiNote;
    noteVelocity = velocity;

    ampEnvelope.setSampleRate (sampleRate);
    ampEnvelope.setSettings (getAmpEnvelopeSettings());
    ampEnvelope.noteOn();

    // Every note gets its own filter envelope, starting from the beginning.
    filterEnvelope.setSampleRate (sampleRate);
    filterEnvelope.setSettings (getFilterEnvelopeSettings());
    filterEnvelope.noteOn();
    filterEnvAmount.reset (sampleRate, filterSmoothingSeconds);
    filterEnvAmount.setCurrentAndTargetValue (parameters.filterEnvAmount.load());

    // A note played while the wheel is already pushed starts at the bent pitch, not gliding to it.
    bendPosition = pitchWheelToBend (currentPitchWheelPosition);
    frequencyRatio.reset (sampleRate, bendSmoothingSeconds);
    frequencyRatio.setCurrentAndTargetValue (getBendRatio());

    // Every note starts with a clean filter, at the current settings rather than gliding to them.
    const auto filterOn = parameters.filterEnabled.load() >= 0.5f;
    filter.setSampleRate (sampleRate);
    filter.reset();
    filterCutoff.reset (sampleRate, filterSmoothingSeconds);
    filterCutoff.setCurrentAndTargetValue (getFilterCutoffTarget());
    filterResonance.reset (sampleRate, filterSmoothingSeconds);
    filterResonance.setCurrentAndTargetValue (parameters.filterResonance.load() / 100.0f);
    filterMix.reset (sampleRate, filterSwitchSeconds);
    filterMix.setCurrentAndTargetValue (filterOn ? 1.0f : 0.0f);
    filter.setCutoffAndQ (envelopedCutoff (filterCutoff.getCurrentValue(), filterEnvAmount.getCurrentValue(), filterEnvelope.getLevel()),
                          Filter::resonanceToQ (filterResonance.getCurrentValue()));
    filterRunning = filterOn;
    filterUpdateCountdown = 0;

    const auto type = getCurrentFilterType();

    for (int i = 0; i < (int) filterTypeAmounts.size(); ++i)
    {
        filterTypeAmounts[(size_t) i].reset (sampleRate, filterSwitchSeconds);
        filterTypeAmounts[(size_t) i].setCurrentAndTargetValue (i == type ? 1.0f : 0.0f);
    }
}

void SynthVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        ampEnvelope.noteOff();
        filterEnvelope.noteOff();
        return;
    }

    // Cut off at once. In practice that means the synth is stealing this voice for a new note
    // because every voice is busy. (It also happens when the sample rate changes, which drops any
    // fading note anyway.) If the note is still sounding, hand it to fadingNote to fade out,
    // rather than stopping it dead.
    if (phaseDelta != 0.0 && ampEnvelope.isActive())
    {
        fadingNote = FadingNote { oscillator,
                                  filter,
                                  filterRunning,
                                  filterMix.getCurrentValue(),
                                  { filterTypeAmounts[0].getCurrentValue(),
                                    filterTypeAmounts[1].getCurrentValue(),
                                    filterTypeAmounts[2].getCurrentValue() },
                                  phaseDelta * frequencyRatio.getCurrentValue(),
                                  noteLevel * ampEnvelope.getLevel(),
                                  1.0f,
                                  (float) (1.0 / (cutOffFadeSeconds * getSampleRate())) };
    }

    clearCurrentNote();
    phaseDelta = 0.0;
}

void SynthVoice::pitchWheelMoved (int newPitchWheelValue)
{
    bendPosition = pitchWheelToBend (newPitchWheelValue);
}

void SynthVoice::renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (fadingNote.has_value())
        renderFadingNote (buffer, startSample, numSamples);

    if (phaseDelta == 0.0)
        return;

    // Picks up both wheel moves and changes to the bend range; either way the pitch glides.
    frequencyRatio.setTargetValue (getBendRatio());

    // A change of waveform during the note fades in rather than switching abruptly.
    oscillator.setTable (getCurrentTable(), juce::roundToInt (waveformCrossfadeSeconds * getSampleRate()));

    // Envelope and filter knob moves apply to the note already playing.
    ampEnvelope.setSettings (getAmpEnvelopeSettings());
    filterEnvelope.setSettings (getFilterEnvelopeSettings());
    updateFilterTargets();

    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        const auto envelopeLevel = ampEnvelope.getNextSample();
        const auto filterEnvelopeLevel = filterEnvelope.getNextSample();
        const auto oscillatorSample = oscillator.getNextSample (phaseDelta * frequencyRatio.getNextValue());
        const auto sample = envelopeLevel * noteLevel * applyFilter (oscillatorSample, filterEnvelopeLevel);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, i, sample);

        if (! ampEnvelope.isActive())
        {
            clearCurrentNote();
            phaseDelta = 0.0;
            break;
        }
    }
}

void SynthVoice::renderFadingNote (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    auto& note = *fadingNote;

    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        auto input = note.oscillator.getNextSample (note.phaseDelta);

        if (note.filterRunning)
        {
            const auto& amounts = note.filterTypeAmounts;
            const auto filtered = blendFilterTypes (note.filter.processAll (input),
                                                    amounts[(size_t) lowPassType],
                                                    amounts[(size_t) highPassType],
                                                    amounts[(size_t) bandPassType]);
            input += note.filterMix * (filtered - input);
        }

        const auto sample = note.gain * note.level * input;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, i, sample);

        note.gain -= note.gainStep;

        if (note.gain <= 0.0f)
        {
            fadingNote.reset();
            return;
        }
    }
}

void SynthVoice::updateFilterTargets()
{
    const auto filterOn = parameters.filterEnabled.load() >= 0.5f;
    filterCutoff.setTargetValue (getFilterCutoffTarget());
    filterResonance.setTargetValue (parameters.filterResonance.load() / 100.0f);
    filterEnvAmount.setTargetValue (parameters.filterEnvAmount.load());

    // A change of filter type crossfades from the old type's output to the new one's.
    const auto type = getCurrentFilterType();

    for (int i = 0; i < (int) filterTypeAmounts.size(); ++i)
        filterTypeAmounts[(size_t) i].setTargetValue (i == type ? 1.0f : 0.0f);

    // Switched on during the note: start the filter clean, at the current settings, and fade it in.
    if (filterOn && ! filterRunning)
    {
        filter.reset();
        filterCutoff.setCurrentAndTargetValue (filterCutoff.getTargetValue());
        filterResonance.setCurrentAndTargetValue (filterResonance.getTargetValue());

        for (auto& amount : filterTypeAmounts)
            amount.setCurrentAndTargetValue (amount.getTargetValue());

        filterEnvAmount.setCurrentAndTargetValue (filterEnvAmount.getTargetValue());
        filter.setCutoffAndQ (envelopedCutoff (filterCutoff.getCurrentValue(), filterEnvAmount.getCurrentValue(), filterEnvelope.getLevel()),
                              Filter::resonanceToQ (filterResonance.getCurrentValue()));
        filterRunning = true;
    }

    // Switched off, and the fade out has finished: stop running it.
    if (! filterOn && filterRunning && ! filterMix.isSmoothing() && filterMix.getCurrentValue() == 0.0f)
        filterRunning = false;

    // applyFilter keeps the coefficients up to date while anything moves the cutoff or resonance. If
    // a change took effect without a glide, bring them up to date here instead. Only when nothing is
    // moving, though: block boundaries depend on the host, and the output mustn't.
    const auto filterEnvelopeHasEffect = filterEnvAmount.isSmoothing() || filterEnvAmount.getTargetValue() != 0.0f;

    if (filterRunning && ! filterCutoff.isSmoothing() && ! filterResonance.isSmoothing() && ! filterEnvelopeHasEffect)
        filter.setCutoffAndQ (envelopedCutoff (filterCutoff.getCurrentValue(), filterEnvAmount.getCurrentValue(), filterEnvelope.getLevel()),
                              Filter::resonanceToQ (filterResonance.getCurrentValue()));

    filterMix.setTargetValue (filterOn ? 1.0f : 0.0f);
}

float SynthVoice::applyFilter (float input, float filterEnvelopeLevel) noexcept
{
    const auto mix = filterMix.getNextValue();

    if (! filterRunning)
        return input;

    // The cutoff moves while its knob or resonance glides, or while the filter envelope has any
    // effect. Then the coefficients are worked out again every filterUpdateInterval samples, plus once
    // more when everything stops, so the filter lands exactly on the final setting.
    auto isMoving = [this]
    {
        return filterCutoff.isSmoothing() || filterResonance.isSmoothing()
            || filterEnvAmount.isSmoothing() || filterEnvAmount.getTargetValue() != 0.0f;
    };

    if (isMoving())
    {
        const auto cutoff = filterCutoff.getNextValue();
        const auto resonance = filterResonance.getNextValue();
        const auto amount = filterEnvAmount.getNextValue();

        if (--filterUpdateCountdown <= 0 || ! isMoving())
        {
            filter.setCutoffAndQ (envelopedCutoff (cutoff, amount, filterEnvelopeLevel), Filter::resonanceToQ (resonance));
            filterUpdateCountdown = filterUpdateInterval;
        }
    }

    const auto filtered = blendFilterTypes (filter.processAll (input),
                                            filterTypeAmounts[(size_t) lowPassType].getNextValue(),
                                            filterTypeAmounts[(size_t) highPassType].getNextValue(),
                                            filterTypeAmounts[(size_t) bandPassType].getNextValue());

    return input + mix * (filtered - input);
}

int SynthVoice::getCurrentFilterType() const
{
    return juce::jlimit (0, filterTypeNames.size() - 1, juce::roundToInt (parameters.filterType.load()));
}

// The cutoff knob's setting, moved by key tracking and velocity. Key tracking at 100 % moves it an
// octave for every octave the note is above or below middle C. Velocity lowers it for softer notes,
// by the Filter Velocity amount for the softest and not at all for the hardest. At 0 % tracking and
// 0 octaves of velocity it stays exactly where the knob says.
float SynthVoice::getFilterCutoffTarget() const
{
    const auto tracking = parameters.filterKeyTracking.load() / 100.0f;
    const auto octavesFromReference = (float) (noteNumber - keyTrackingReferenceNote) / 12.0f;
    const auto velocityOctaves = -parameters.filterVelocity.load() * (1.0f - noteVelocity);
    const auto cutoff = parameters.filterCutoff.load() * std::exp2 (octavesFromReference * tracking + velocityOctaves);
    return std::clamp (cutoff, minCutoffHz, maxCutoffHz);
}

void SynthVoice::setCurrentPlaybackSampleRate (double newRate)
{
    // A fading note's pitch and fade were worked out for the old sample rate.
    fadingNote.reset();
    SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
}

double SynthVoice::getBendRatio() const
{
    return std::exp2 (bendPosition * parameters.pitchBendRange.load() / 12.0);
}

const BandLimitedWaveTable& SynthVoice::getCurrentTable() const
{
    return tables.get (juce::roundToInt (parameters.waveform.load()));
}

Envelope::Settings SynthVoice::getAmpEnvelopeSettings() const
{
    return { parameters.ampAttack.load(),
             parameters.ampDecay.load(),
             parameters.ampSustain.load() / 100.0f,
             parameters.ampRelease.load() };
}

Envelope::Settings SynthVoice::getFilterEnvelopeSettings() const
{
    return { parameters.filterAttack.load(),
             parameters.filterDecay.load(),
             parameters.filterSustain.load() / 100.0f,
             parameters.filterRelease.load() };
}

} // namespace boringsynth
