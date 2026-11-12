#pragma once

#include "Waveforms.h"
#include "WavetableImporter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>

namespace boringsynth
{

// A small picture of the waveform playing: one cycle of it at the current position in its frames, scaled
// to the loudest sample anywhere in its table, so moving through a table never changes the scale. On a
// wavetable, a marker along the bottom shows the position.
//
// The position is that of the newest note playing, its envelope and the LFO included, and without a note
// the knob's. It only reads these values, 30 times a second, so it can't change the sound. Only touched
// on the message thread.
class WavetableDisplay  : public juce::Component,
                          private juce::Timer
{
public:
    // All of these must outlive the display. waveform is a position in waveformNames, knob 0 to 100 %,
    // and playedPosition 0 to 1, or negative with no note playing.
    WavetableDisplay (const WaveformTables& waveformTables, const WavetableImporter& importer, const std::atomic<float>& waveform,
                      const std::atomic<float>& knob, const std::atomic<float>& playedPosition);

    // Reads the values again, and redraws if they've changed. The timer calls this.
    void refresh();

    int getShownWaveform() const noexcept { return shownWaveform; }
    float getShownPosition() const noexcept { return shownPosition; }

    // The loudest sample in any of a table's frames, at its richest level.
    static float getPeak (const MultiFrameWaveTable& table);

    void paint (juce::Graphics& g) override;

private:
    const WaveformTables& tables;
    const WavetableImporter& wavetableImporter;
    const std::atomic<float>& waveformValue;
    const std::atomic<float>& knobValue;
    const std::atomic<float>& playedPositionValue;

    int shownWaveform = -1; // none yet, so the first refresh works out the peak
    int shownGeneration = 0; // the importer's, as a new table there needs a new peak
    float shownPosition = 0.0f;
    float peak = 1.0f;

    void timerCallback() override { refresh(); }

    // Calls reader with the table for a waveform, holding a reference to an imported one for as long as it
    // takes, so it can't be freed part-way through.
    void readTable (int waveform, const std::function<void (const MultiFrameWaveTable&)>& reader) const;
    void drawTable (juce::Graphics& g, const MultiFrameWaveTable& table) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableDisplay)
};

} // namespace boringsynth
