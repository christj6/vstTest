#include "TestHelpers.h"

#include "BoringSynthProcessor.h"

// The --render tool: plays a MIDI file through the plug-in, with one of its factory presets, and
// writes what comes out to a WAV file. For reproducing what someone heard in their DAW, exactly.
// Also its tests, which check the WAV is exactly what the plug-in plays.

namespace testing
{

namespace
{

// A folder for the tests' files, deleted afterwards.
struct TemporaryFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("BoringSynthRenderTests-" + juce::String (juce::Random::getSystemRandom().nextInt64()));

    TemporaryFolder() { folder.createDirectory(); }
    ~TemporaryFolder() { folder.deleteRecursively(); }
};

std::vector<float> readWav (const juce::File& file)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));

    if (reader == nullptr)
        return {};

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return { buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples() };
}

// Writes a MIDI file of one track, timed at 96 ticks to the quarter note, into the folder.
juce::File writeMidiFile (const juce::File& folder, const juce::MidiMessageSequence& track)
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (96);
    midi.addTrack (track);
    const auto file = folder.getChildFile ("test.mid");
    file.deleteFile();
    juce::FileOutputStream stream (file);
    midi.writeTo (stream);
    stream.flush();
    return file;
}

void testRenderToolPlaysTheMidiFile()
{
    // A MIDI file at 100 BPM (96 ticks to the quarter note): two notes, a sixteenth apart, then off.
    TemporaryFolder temporary;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (600000), 0.0);
    track.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100), 0.0);
    track.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 80), 24.0);
    track.addEvent (juce::MidiMessage::noteOff (1, 45), 192.0);
    track.addEvent (juce::MidiMessage::noteOff (1, 57), 192.0);
    const auto midiFile = writeMidiFile (temporary.folder, track);

    const auto wavFile = temporary.folder.getChildFile ("test.wav");
    const auto result = renderMidiFile (midiFile.getFullPathName().toRawUTF8(), wavFile.getFullPathName().toRawUTF8(), "Pluck Bass",
                                        juce::StringArray ("filterCutoff=500"));
    const auto rendered = readWav (wavFile);

    // The same notes played directly, at the same times: a sixteenth at 100 BPM is 0.15 s.
    constexpr double sampleRate = 44100.0;
    const auto direct = render ({ .sampleRate = sampleRate,
                                  .numSamples = (int) rendered.size(),
                                  .events = { { 0, noteOn (45, 100) }, { juce::roundToInt (0.15 * sampleRate), noteOn (57, 80) },
                                              { juce::roundToInt (1.2 * sampleRate), noteOff (45) }, { juce::roundToInt (1.2 * sampleRate), noteOff (57) } },
                                  .setUp = [] (juce::AudioProcessor& p)
                                  {
                                      dynamic_cast<boringsynth::BoringSynthProcessor&> (p).getPresetManager().loadFactoryPreset (2);
                                      setParameter (p, "filterCutoff", 500.0f);
                                  },
                                  .transport = [] (int blockStart)
                                  {
                                      juce::AudioPlayHead::PositionInfo position;
                                      position.setBpm (100.0);
                                      position.setPpqPosition (blockStart / sampleRate * 100.0 / 60.0);
                                      position.setIsPlaying (true);
                                      return position;
                                  } });

    std::printf ("      rendered %.3f s\n", rendered.size() / sampleRate);
    check (result == 0 && rendered.size() > (size_t) (1.2 * sampleRate) && rendered == direct,
           "--render plays a MIDI file through the plug-in with a preset and changes, exactly as the plug-in plays it");

    const auto missing = temporary.folder.getChildFile ("missing.wav");
    check (renderMidiFile ("no such file.mid", missing.getFullPathName().toRawUTF8(), "Init", {}) != 0
               && renderMidiFile (midiFile.getFullPathName().toRawUTF8(), missing.getFullPathName().toRawUTF8(), "No Such Preset", {}) != 0
               && renderMidiFile (midiFile.getFullPathName().toRawUTF8(), missing.getFullPathName().toRawUTF8(), "Init", juce::StringArray ("noSuchParameter=1")) != 0
               && ! missing.exists(),
           "--render refuses a missing MIDI file, an unknown preset or an unknown parameter, writing nothing");
}

void testRenderToolWaitsForLongReleases()
{
    // One note, let go after a quarter at 100 BPM (0.6 s), with a 3 s release. The WAV carries on
    // until the release has finished: the note is still sounding 2.5 s after it was let go, and
    // silent by the end.
    TemporaryFolder temporary;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (600000), 0.0);
    track.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0.0);
    track.addEvent (juce::MidiMessage::noteOff (1, 57), 96.0);
    const auto midiFile = writeMidiFile (temporary.folder, track);
    const auto wavFile = temporary.folder.getChildFile ("test.wav");
    renderMidiFile (midiFile.getFullPathName().toRawUTF8(), wavFile.getFullPathName().toRawUTF8(), "Init",
                    juce::StringArray ("ampRelease=3"));
    const auto rendered = readWav (wavFile);

    constexpr double sampleRate = 44100.0;
    auto loudestBetween = [&] (double from, double to)
    {
        float loudest = 0.0f;

        for (auto i = (size_t) (from * sampleRate); i < std::min (rendered.size(), (size_t) (to * sampleRate)); ++i)
            loudest = std::max (loudest, std::abs (rendered[i]));

        return loudest;
    };

    const auto seconds = rendered.size() / sampleRate;
    const auto lateInRelease = loudestBetween (3.0, 3.2);
    const auto atTheEnd = loudestBetween (seconds - 0.1, seconds);
    std::printf ("      rendered %.3f s; loudest 2.4 to 2.6 s after the note was let go %.4f, in the last 0.1 s %.1e\n",
                 seconds, lateInRelease, atTheEnd);
    check (seconds >= 3.6 && lateInRelease > 0.005f && atTheEnd == 0.0f,
           "--render carries on until a long release has finished");
}

void testRenderToolFollowsTempoChanges()
{
    // A file at 100 BPM for four quarters (2.4 s), then 150 BPM: one note through both, with Chopped
    // Chords, whose LFO is synced to the tempo. The WAV is what a host following the tempo map plays,
    // reporting each tempo and the song's position, worked out here by hand.
    TemporaryFolder temporary;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (600000), 0.0);
    track.addEvent (juce::MidiMessage::tempoMetaEvent (400000), 384.0);
    track.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0.0);
    track.addEvent (juce::MidiMessage::noteOff (1, 57), 768.0);
    const auto midiFile = writeMidiFile (temporary.folder, track);
    const auto wavFile = temporary.folder.getChildFile ("test.wav");
    renderMidiFile (midiFile.getFullPathName().toRawUTF8(), wavFile.getFullPathName().toRawUTF8(), "Chopped Chords", {});
    const auto rendered = readWav (wavFile);

    constexpr double sampleRate = 44100.0;
    const auto loadChoppedChords = [] (juce::AudioProcessor& p)
    {
        auto& presets = dynamic_cast<boringsynth::BoringSynthProcessor&> (p).getPresetManager();

        for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
            if (presets.getFactoryPresetName (i) == "Chopped Chords")
                presets.loadFactoryPreset (i);
    };

    const auto direct = render ({ .sampleRate = sampleRate,
                                  .numSamples = (int) rendered.size(),
                                  .events = { { 0, noteOn (57, 100) }, { juce::roundToInt (4.0 * sampleRate), noteOff (57) } },
                                  .setUp = loadChoppedChords,
                                  .transport = [] (int blockStart)
                                  {
                                      const auto seconds = blockStart / sampleRate;
                                      juce::AudioPlayHead::PositionInfo position;
                                      position.setBpm (seconds < 2.4 ? 100.0 : 150.0);
                                      position.setPpqPosition (seconds < 2.4 ? seconds / 0.6 : 4.0 + (seconds - 2.4) / 0.4);
                                      position.setIsPlaying (true);
                                      return position;
                                  } });

    float largestDifference = rendered.size() == direct.size() ? 0.0f : 1.0f;

    for (size_t i = 0; i < std::min (rendered.size(), direct.size()); ++i)
        largestDifference = std::max (largestDifference, std::abs (rendered[i] - direct[i]));

    std::printf ("      rendered %.3f s; largest difference from a host following the tempo map %.1e\n",
                 rendered.size() / sampleRate, largestDifference);
    check (largestDifference < 1.0e-6f, "--render follows the MIDI file's tempo changes, as a host would");
}

} // namespace

void runRenderToolTests()
{
    testRenderToolPlaysTheMidiFile();
    testRenderToolWaitsForLongReleases();
    testRenderToolFollowsTempoChanges();
}

TempoMap::TempoMap (const juce::MidiFile& midi)
{
    // Every tempo event in every track, by position in ticks, each the seconds per quarter note from
    // there on. Without any, 120 BPM throughout. A file timed in SMPTE frames rather than ticks to the
    // quarter note has no tempo either, so it gets 120 BPM too.
    std::vector<std::pair<double, double>> tempoEvents;
    const auto ticksPerQuarter = (double) midi.getTimeFormat();

    if (ticksPerQuarter > 0.0)
        for (int track = 0; track < midi.getNumTracks(); ++track)
            for (const auto* event : *midi.getTrack (track))
                if (event->message.isTempoMetaEvent())
                    tempoEvents.emplace_back (event->message.getTimeStamp(), event->message.getTempoSecondsPerQuarterNote());

    std::stable_sort (tempoEvents.begin(), tempoEvents.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [ticks, secondsPerQuarter] : tempoEvents)
    {
        const auto quarterNotes = ticks / ticksPerQuarter;
        const auto& previous = changes.back();
        const auto seconds = previous.seconds + (quarterNotes - previous.quarterNotes) * previous.secondsPerQuarter;

        // A tempo event at the same place as the last one replaces it.
        if (quarterNotes == previous.quarterNotes)
            changes.back().secondsPerQuarter = secondsPerQuarter;
        else
            changes.push_back ({ seconds, quarterNotes, secondsPerQuarter });
    }
}

juce::AudioPlayHead::PositionInfo TempoMap::getPositionAt (double seconds) const
{
    // The last change at or before this time.
    auto change = changes.front();

    for (const auto& each : changes)
        if (each.seconds <= seconds)
            change = each;

    juce::AudioPlayHead::PositionInfo position;
    position.setBpm (60.0 / change.secondsPerQuarter);
    position.setPpqPosition (change.quarterNotes + (seconds - change.seconds) / change.secondsPerQuarter);
    position.setIsPlaying (true);
    return position;
}

int renderMidiFile (const char* midiPath, const char* wavPath, const char* presetName, const juce::StringArray& overrides)
{
    constexpr double sampleRate = 44100.0;

    // After the last MIDI event, for releases to finish: the plug-in's tail (its amp release), plus a
    // little, and at least a second.
    constexpr double minimumTailSeconds = 1.0;
    constexpr double extraTailSeconds = 0.25;

    const auto midiFile = juce::File::getCurrentWorkingDirectory().getChildFile (midiPath);
    juce::FileInputStream input (midiFile);
    juce::MidiFile midi;

    if (! input.openedOk() || ! midi.readFrom (input))
    {
        std::printf ("couldn't read %s as a MIDI file\n", midiFile.getFullPathName().toRawUTF8());
        return 1;
    }

    // The host follows the file's tempo map (120 BPM if it has none), from the start of the song.
    const TempoMap tempoMap (midi);
    midi.convertTimestampTicksToSeconds();
    std::vector<TimedMessage> events;
    int lastEvent = 0;

    for (int track = 0; track < midi.getNumTracks(); ++track)
        for (const auto* event : *midi.getTrack (track))
            if (! event->message.isMetaEvent())
            {
                events.push_back ({ juce::roundToInt (event->message.getTimeStamp() * sampleRate), event->message });
                lastEvent = std::max (lastEvent, events.back().samplePosition);
            }

    std::unique_ptr<juce::AudioProcessor> probe (createPluginFilter());
    auto& presets = dynamic_cast<boringsynth::BoringSynthProcessor&> (*probe).getPresetManager();
    int presetIndex = -1;

    for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
        if (presets.getFactoryPresetName (i) == presetName)
            presetIndex = i;

    if (presetIndex < 0)
    {
        std::printf ("no factory preset named \"%s\"\n", presetName);
        return 1;
    }

    // Changes to the preset, each "parameterId=value" in the parameter's own units.
    for (const auto& change : overrides)
        if (! change.contains ("=") || findParameter (*probe, change.upToFirstOccurrenceOf ("=", false, false)) == nullptr)
        {
            std::printf ("\"%s\" isn't parameterId=value for a parameter the plug-in has\n", change.toRawUTF8());
            return 1;
        }

    const auto setUp = [presetIndex, overrides] (juce::AudioProcessor& p)
    {
        dynamic_cast<boringsynth::BoringSynthProcessor&> (p).getPresetManager().loadFactoryPreset (presetIndex);

        for (const auto& change : overrides)
            setParameter (p, change.upToFirstOccurrenceOf ("=", false, false), change.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
    };

    setUp (*probe);
    const auto tailSeconds = std::max (minimumTailSeconds, probe->getTailLengthSeconds() + extraTailSeconds);

    const auto output = render ({ .sampleRate = sampleRate,
                                  .numSamples = lastEvent + (int) (tailSeconds * sampleRate),
                                  .events = events,
                                  .setUp = setUp,
                                  .transport = [&tempoMap] (int blockStart) { return tempoMap.getPositionAt (blockStart / sampleRate); } });

    // Mono, as 32-bit float, so nothing is lost to rounding.
    const auto wavFile = juce::File::getCurrentWorkingDirectory().getChildFile (wavPath);
    wavFile.deleteFile();
    juce::AudioBuffer<float> buffer (1, (int) output.size());
    buffer.copyFrom (0, 0, output.data(), (int) output.size());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = wavFile.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer;

    if (stream != nullptr)
        writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (1).withBitsPerSample (32)
                                                  .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));

    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
    {
        std::printf ("couldn't write %s\n", wavFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto tempoChanges = (int) tempoMap.getChanges().size() - 1;
    std::printf ("rendered %d MIDI events from %.2f BPM%s with \"%s\"%s%s to %s (%.3f s)\n",
                 (int) events.size(), 60.0 / tempoMap.getChanges().front().secondsPerQuarter,
                 tempoChanges > 0 ? (", with " + juce::String (tempoChanges) + (tempoChanges == 1 ? " tempo change" : " tempo changes")).toRawUTF8() : "",
                 presetName, overrides.isEmpty() ? "" : ", changed: ", overrides.joinIntoString (" ").toRawUTF8(),
                 wavFile.getFullPathName().toRawUTF8(), output.size() / sampleRate);
    return 0;
}

} // namespace testing
