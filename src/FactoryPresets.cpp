#include "FactoryPresets.h"

#include "Lfo.h"
#include "Parameters.h"
#include "Waveforms.h"

namespace boringsynth
{

const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },

        // Slow to start and to fade, a little darker, with a gentle vibrato.
        { "Soft Pad", { { waveformId, (float) triangleWaveform },
                        { ampAttackId, 0.6f }, { ampDecayId, 0.5f }, { ampSustainId, 80.0f }, { ampReleaseId, 1.2f },
                        { filterEnabledId, 1.0f }, { filterCutoffId, 1800.0f }, { filterResonanceId, 10.0f },
                        { lfoRateId, 4.5f }, { lfoDepthId, 12.0f },
                        { velocitySensitivityId, 40.0f } } },

        // A short saw whose filter snaps shut, brighter the harder it's played.
        { "Pluck Bass", { { waveformId, (float) sawWaveform },
                          { ampDecayId, 0.45f }, { ampSustainId, 0.0f }, { ampReleaseId, 0.08f },
                          { filterEnabledId, 1.0f }, { filterCutoffId, 180.0f }, { filterResonanceId, 35.0f },
                          { filterKeyTrackingId, 50.0f }, { filterVelocityId, 2.0f },
                          { filterDecayId, 0.25f }, { filterSustainId, 0.0f }, { filterEnvAmountId, 4.0f },
                          { velocitySensitivityId, 60.0f } } },

        // A resonant filter swept fast by its envelope: the classic acid "squelch".
        { "Squelch", { { waveformId, (float) sawWaveform },
                       { ampReleaseId, 0.05f },
                       { filterEnabledId, 1.0f }, { filterCutoffId, 120.0f }, { filterResonanceId, 80.0f },
                       { filterVelocityId, 3.0f },
                       { filterDecayId, 0.18f }, { filterSustainId, 10.0f }, { filterEnvAmountId, 5.0f },
                       { velocitySensitivityId, 50.0f } } },

        // A square through a mellow filter, its level pulsing with the LFO.
        { "Tremolo Organ", { { waveformId, (float) squareWaveform },
                             { ampAttackId, 0.01f }, { ampReleaseId, 0.1f },
                             { filterEnabledId, 1.0f }, { filterCutoffId, 4000.0f },
                             { lfoDestinationId, (float) volumeDestination }, { lfoRateId, 6.0f }, { lfoDepthId, 45.0f } } },

        // The LFO sweeping a resonant filter in eighth notes, starting afresh with each note.
        { "Wobble Bass", { { waveformId, (float) sawWaveform },
                           { ampReleaseId, 0.05f },
                           { filterEnabledId, 1.0f }, { filterCutoffId, 250.0f }, { filterResonanceId, 60.0f },
                           { lfoDestinationId, (float) cutoffDestination }, { lfoDepthId, 60.0f },
                           { lfoSyncId, 1.0f }, { lfoSyncRateId, 10.0f }, { lfoRetriggerId, 1.0f } } },

        // A bright saw with no vibrato until the mod wheel brings it in.
        { "Vibrato Lead", { { waveformId, (float) sawWaveform },
                            { ampAttackId, 0.02f }, { ampReleaseId, 0.15f },
                            { filterEnabledId, 1.0f }, { filterCutoffId, 3000.0f }, { filterResonanceId, 25.0f },
                            { filterKeyTrackingId, 100.0f },
                            { lfoRateId, 5.5f }, { modWheelLfoDepthId, 25.0f },
                            { velocitySensitivityId, 50.0f } } },

        // Chords chopped into sixteenth notes by a square LFO on the volume, in time with the song.
        { "Chopped Chords", { { waveformId, (float) sawWaveform },
                              { ampReleaseId, 0.3f },
                              { filterEnabledId, 1.0f }, { filterCutoffId, 2500.0f },
                              { lfoDestinationId, (float) volumeDestination }, { lfoShapeId, (float) squareLfoShape },
                              { lfoDepthId, 100.0f }, { lfoSyncId, 1.0f }, { lfoSyncRateId, 13.0f }, { lfoRetriggerId, 1.0f } } },

        // A pure sine for deep bass under other sounds: no harmonics to buzz, held while the key is
        // down, fading over 120 ms when it's released. A little quieter when played softly.
        { "Sub Bass", { { waveformId, (float) sineWaveform },
                        { ampReleaseId, 0.12f },
                        { velocitySensitivityId, 30.0f } } },

        // Odd harmonics only, for a woody, clarinet-like tone, with a gentle vibrato.
        { "Hollow Reed", { { waveformId, (float) fmHollowWaveform }, { wavetablePositionId, 25.0f },
                           { ampAttackId, 0.03f }, { ampReleaseId, 0.12f },
                           { lfoRateId, 5.0f }, { lfoDepthId, 8.0f },
                           { velocitySensitivityId, 40.0f } } },

        // A bright, tearing sync sound that dies away, with its filter closing behind it.
        { "Sync Stab", { { waveformId, (float) hardSyncWaveform }, { wavetablePositionId, 45.0f },
                         { ampDecayId, 0.35f }, { ampSustainId, 30.0f }, { ampReleaseId, 0.15f },
                         { filterEnabledId, 1.0f }, { filterCutoffId, 1200.0f }, { filterResonanceId, 20.0f },
                         { filterDecayId, 0.3f }, { filterSustainId, 0.0f }, { filterEnvAmountId, 3.0f },
                         { velocitySensitivityId, 50.0f } } },

        // A slow swell with a vowel-like hump of harmonics, moving gently with the vibrato.
        { "Vowel Pad", { { waveformId, (float) formantWaveform }, { wavetablePositionId, 50.0f },
                         { ampAttackId, 0.5f }, { ampDecayId, 0.5f }, { ampSustainId, 85.0f }, { ampReleaseId, 1.0f },
                         { lfoRateId, 4.5f }, { lfoDepthId, 10.0f },
                         { velocitySensitivityId, 30.0f } } },

        // A few folds of a sine for a thick, buzzy bass, its filter snapping shut.
        { "Folded Bass", { { waveformId, (float) wavefoldWaveform }, { wavetablePositionId, 20.0f },
                           { ampReleaseId, 0.08f },
                           { filterEnabledId, 1.0f }, { filterCutoffId, 400.0f }, { filterResonanceId, 25.0f },
                           { filterKeyTrackingId, 50.0f },
                           { filterDecayId, 0.25f }, { filterSustainId, 15.0f }, { filterEnvAmountId, 3.0f },
                           { velocitySensitivityId, 50.0f } } } };

    return presets;
}

} // namespace boringsynth
