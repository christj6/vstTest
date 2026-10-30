#pragma once

#include <algorithm>

namespace boringsynth
{

// An ADSR envelope: how a note's level changes over time. It rises from 0 to full level over the
// attack time, falls to the sustain level over the decay time, holds there while the key is down,
// and falls to 0 over the release time once the key is let go. Every stage is a straight line.
//
// It differs from juce::ADSR in three ways, each to avoid a click:
//  - Changing the sustain level while a note holds glides to the new level instead of jumping.
//  - The release always starts from the level the note is actually at, and always takes the release
//    time. (juce::ADSR recalculates it from the sustain level whenever its settings are updated,
//    which cuts a note off instantly when the sustain level is 0.)
//  - Settings can be updated on every block without disturbing a note in progress.
class Envelope
{
public:
    struct Settings
    {
        float attackSeconds = 0.0f;
        float decaySeconds = 0.0f;
        float sustainLevel = 1.0f; // 0 to 1
        float releaseSeconds = 0.0f;
    };

    // How long a change of sustain level takes to glide through the whole range.
    static constexpr double sustainGlideSeconds = 0.02;

    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        sustainGlideStep = (float) (1.0 / (sustainGlideSeconds * sampleRate));
        updateSteps();
    }

    // Takes effect from the next sample; call it as often as the settings might change.
    void setSettings (const Settings& newSettings) noexcept
    {
        settings = newSettings;
        updateSteps();
    }

    // Starts the attack from silence. With an attack time of 0 the note starts at full level.
    void noteOn() noexcept
    {
        if (settings.attackSeconds > 0.0f)
        {
            level = 0.0f;
            stage = Stage::attack;
        }
        else
        {
            level = 1.0f;
            stage = Stage::decay;
        }
    }

    // Starts the release from wherever the level is now. The release lasts releaseSeconds from
    // here, whatever stage the note was in; changing the release time afterwards doesn't change a
    // release already under way. (A new attack or decay time, by contrast, changes the slope of a
    // stage already under way, from where it is: it doesn't then take that time from there.)
    void noteOff() noexcept
    {
        if (stage == Stage::idle)
            return;

        if (settings.releaseSeconds <= 0.0f || level <= 0.0f)
        {
            level = 0.0f;
            stage = Stage::idle;
            return;
        }

        releaseStep = (float) (level / (settings.releaseSeconds * sampleRate));
        stage = Stage::release;
    }

    // Returns the level for this sample (0 to 1), then moves on to the next one.
    float getNextSample() noexcept
    {
        const auto current = level;

        switch (stage)
        {
            case Stage::idle:
                break;

            case Stage::attack:
                level += attackStep;

                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }

                break;

            case Stage::decay:
                level -= decayStep;

                if (level <= settings.sustainLevel)
                {
                    level = settings.sustainLevel;
                    stage = Stage::sustain;
                }

                break;

            case Stage::sustain:
                // Glide towards the sustain level, in case it has changed.
                if (level < settings.sustainLevel)
                    level = std::min (level + sustainGlideStep, settings.sustainLevel);
                else if (level > settings.sustainLevel)
                    level = std::max (level - sustainGlideStep, settings.sustainLevel);

                break;

            case Stage::release:
                level -= releaseStep;

                if (level <= 0.0f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }

                break;
        }

        return current;
    }

    // False once the release has finished (or before the first note).
    bool isActive() const noexcept { return stage != Stage::idle; }

    // The level the next sample will have, without moving on.
    float getLevel() const noexcept { return level; }

private:
    enum class Stage { idle, attack, decay, sustain, release };

    void updateSteps() noexcept
    {
        if (sampleRate <= 0.0)
            return;

        // An attack or decay time of 0 takes a single sample.
        attackStep = settings.attackSeconds > 0.0f ? (float) (1.0 / (settings.attackSeconds * sampleRate)) : 1.0f;
        decayStep = settings.decaySeconds > 0.0f ? (float) ((1.0 - settings.sustainLevel) / (settings.decaySeconds * sampleRate)) : 1.0f;

        // If the sustain level is raised above a note still in its decay, there's nothing left to
        // decay: let the sustain stage glide it up instead.
        if (stage == Stage::decay && level <= settings.sustainLevel)
            stage = Stage::sustain;
    }

    Settings settings;
    double sampleRate = 0.0;
    float attackStep = 1.0f;
    float decayStep = 1.0f;
    float releaseStep = 0.0f;
    float sustainGlideStep = 1.0f;
    float level = 0.0f;
    Stage stage = Stage::idle;
};

} // namespace boringsynth
