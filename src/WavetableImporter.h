#pragma once

#include "MultiFrameWaveTable.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace boringsynth
{

// Loads a wavetable from a file for the Imported waveform, on a thread of its own, as reading a file and
// band-limiting its frames takes far too long for anywhere else. Each copy of the plug-in has its own.
//
// The audio thread only ever reads one atomic pointer (getTable), so it never waits or allocates. A new
// table replaces the old one by swapping that pointer. Voices move to the new table at their next block,
// fading from the old one for 10 ms, and a note cut off fades for another 5 ms still reading it. So the
// old table is kept until the audio has rendered two more whole blocks, and far more samples than those
// fades take at any sample rate, and only then freed, on this thread. If audio stops, it waits.
//
// A file's channels are mixed to one. A whole number of 2048-sample frames, up to 256, is split into those
// frames. Anything else up to 8192 samples is one cycle, resampled to 2048 samples. Each frame has its DC
// offset removed and is scaled to the sine's RMS, unless that would take its peak past 3 times the
// sine's. A silent frame stays silent.
class WavetableImporter  : private juce::Thread
{
public:
    static constexpr int maxFrames = 256;
    static constexpr int maxSingleCycleSamples = 8192;

    WavetableImporter();
    ~WavetableImporter() override;

    // Starts loading a file. The latest wins, if one's already waiting. Not on the audio thread.
    void import (const juce::File& file);

    // Whether a file is still being loaded.
    bool isBusy() const noexcept { return busy.load(); }

    // How the latest import went: the name of the file now loaded, if any, and why the latest failed, if
    // it did. A failed import leaves the table as it was.
    struct Status
    {
        juce::String fileName;
        juce::String error;
    };

    Status getStatus() const;

    // The loaded table, or null. The table is only safe to read on the audio thread, for as long as
    // explained above; anywhere else, use readTable.
    const MultiFrameWaveTable* getTable() const noexcept { return current.load (std::memory_order_acquire); }

    // Calls reader with the loaded table, or null, keeping it safe for as long as reader takes. Not on
    // the audio thread.
    void readTable (const std::function<void (const MultiFrameWaveTable*)>& reader) const;

    // Goes up by one each time a new table's loaded.
    int getGeneration() const noexcept { return generation.load(); }

    // The audio thread calls this after each block, so old tables can be freed once it's safe.
    void blockRendered (int numSamples) noexcept;

    // How many old tables are still waiting to be freed.
    int getNumOldTables() const;

    // What a file's frames would be, processed as above, or empty with the reason why not.
    struct Cycles
    {
        std::vector<std::vector<float>> frames;
        juce::String error;
    };

    static Cycles readCycles (const juce::File& file);

private:
    struct OldTable
    {
        std::unique_ptr<MultiFrameWaveTable> table;
        juce::uint64 blocksWhenReplaced;
        juce::uint64 samplesWhenReplaced;
    };

    juce::CriticalSection lock; // never taken on the audio thread
    juce::File pendingFile;
    Status status;
    std::unique_ptr<MultiFrameWaveTable> table;
    std::vector<OldTable> oldTables;

    std::atomic<const MultiFrameWaveTable*> current { nullptr };
    std::atomic<int> generation { 0 };
    std::atomic<bool> busy { false };
    std::atomic<juce::uint64> blocksRendered { 0 };
    std::atomic<juce::uint64> samplesRendered { 0 };

    void run() override;
    void loadFile (const juce::File& file);
    void freeOldTables();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableImporter)
};

} // namespace boringsynth
