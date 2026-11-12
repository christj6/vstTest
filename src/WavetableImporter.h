#pragma once

#include "MultiFrameWaveTable.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <memory>
#include <vector>

namespace boringsynth
{

// One loaded wavetable, never changed once built, with the frames it was built from (for saving) and the
// name of its file. Shared, by reference count, between the importer, the voices playing it and the window
// drawing it. One with no frames stands for having no table loaded.
class ImportedTable  : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<ImportedTable>;

    ImportedTable (std::vector<std::vector<float>> frames, const juce::String& fileName, int number);

    // Null for one standing for no table.
    const MultiFrameWaveTable* getTable() const noexcept { return table.get(); }

    const std::vector<std::vector<float>>& getCycles() const noexcept { return cycles; }
    const juce::String& getFileName() const noexcept { return name; }

    // Each table loaded gets the next number, starting from 1.
    int getGeneration() const noexcept { return generation; }

private:
    std::vector<std::vector<float>> cycles;
    std::unique_ptr<MultiFrameWaveTable> table;
    juce::String name;
    int generation;
};

// Loads a wavetable from a file for the Imported waveform, on a thread of its own, as reading a file and
// band-limiting its frames takes far too long for anywhere else. Each copy of the plug-in has its own.
//
// Handing a table to the audio thread, and freeing it later, is the usual reference-counted "release
// pool": the importer keeps a reference to every table that might still be in use, so whoever else lets
// go of one, the audio thread included, is never the last, and nothing is ever freed on the audio thread.
// The audio thread picks up the newest table at the start of each block (prepareForBlock), without
// waiting or allocating. Voices hold references to the tables they're reading, crossfades and notes
// fading out included. The importer frees a table once nothing but itself holds it, and the audio thread
// has moved on to a newer one, so can't pick it up again. Nothing depends on how long anything takes.
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

    // The newest table loaded (or null), for anything but the audio thread: keep the reference for as long
    // as you read it.
    ImportedTable::Ptr getLatest() const;

    // The newest table's wavetable, or null. Not on the audio thread; it stays valid until another loads.
    const MultiFrameWaveTable* getTable() const;

    // Goes up by one each time a new table's loaded, or the table's cleared.
    int getGeneration() const noexcept { return generation.load(); }

    // The audio thread calls this at the start of each block, to pick up a newly loaded table.
    void prepareForBlock() noexcept;

    // The table the audio thread is using (or null). Only on the audio thread.
    const ImportedTable::Ptr& getAudioTable() const noexcept { return audioTable; }

    // How many tables other than the newest are still kept, as something might still be reading them.
    int getNumOldTables() const;

    // The loaded table is saved with projects and presets, embedded: a child of the saved state named
    // stateChildId, holding the file's name and its frames as processed, as 32-bit little-endian floats
    // in base64, so it loads the same even if the file has moved or gone. Not on the audio thread.
    static constexpr const char* stateChildId = "ImportedWavetable";
    void writeTo (juce::ValueTree& state) const;

    // Loads the table saved in a state, if it holds one, building it before returning, so a host that
    // starts rendering straight away hears it. Without one, a project clears the table and a preset keeps
    // it. A saved table that can't be read clears it, saying why. Not on the audio thread.
    enum class IfNoneSaved { clear, keep };
    void readFrom (const juce::ValueTree& state, IfNoneSaved ifNoneSaved);

    // Unloads the table, so Imported plays a sine. Not on the audio thread.
    void clear();

    // What a file's frames would be, processed as above, or empty with the reason why not.
    struct Cycles
    {
        std::vector<std::vector<float>> frames;
        juce::String error;
    };

    static Cycles readCycles (const juce::File& file);

private:
    juce::CriticalSection lock;        // never taken on the audio thread
    juce::CriticalSection publishLock; // nor this, held while a new table's made
    juce::File pendingFile;
    Status status;

    // Every table that might still be in use, oldest first: the last is the newest.
    std::vector<ImportedTable::Ptr> tables;

    // The newest table, until the audio thread picks it up, and the generation of the last one it did.
    std::atomic<ImportedTable*> waiting { nullptr };
    std::atomic<int> takenByAudio { 0 };

    std::atomic<int> generation { 0 };
    std::atomic<bool> busy { false };

    ImportedTable::Ptr audioTable; // only touched on the audio thread

    void run() override;
    void loadFile (const juce::File& file);
    void freeUnusedTables();

    // Adds a new table (or one standing for none), and hands it to the audio thread.
    void publish (std::vector<std::vector<float>> frames, const Status& newStatus);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableImporter)
};

} // namespace boringsynth
