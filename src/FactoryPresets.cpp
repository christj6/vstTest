#include "FactoryPresets.h"

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
                              { lfoDestinationId, (float) volumeDestination }, { lfoShapeId, (float) squareWaveform },
                              { lfoDepthId, 100.0f }, { lfoSyncId, 1.0f }, { lfoSyncRateId, 13.0f }, { lfoRetriggerId, 1.0f } } } };

    return presets;
}

} // namespace boringsynth
