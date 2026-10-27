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
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (96);
    midi.addTrack (track);
    const auto midiFile = temporary.folder.getChildFile ("test.mid");
    juce::FileOutputStream stream (midiFile);
    midi.writeTo (stream);
    stream.flush();

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

} // namespace

void runRenderToolTests()
{
    testRenderToolPlaysTheMidiFile();
}

int renderMidiFile (const char* midiPath, const char* wavPath, const char* presetName, const juce::StringArray& overrides)
{
    constexpr double sampleRate = 44100.0;
    constexpr double tailSeconds = 1.0; // after the last MIDI event, for releases to finish

    const auto midiFile = juce::File::getCurrentWorkingDirectory().getChildFile (midiPath);
    juce::FileInputStream input (midiFile);
    juce::MidiFile midi;

    if (! input.openedOk() || ! midi.readFrom (input))
    {
        std::printf ("couldn't read %s as a MIDI file\n", midiFile.getFullPathName().toRawUTF8());
        return 1;
    }

    // The host plays at the file's first tempo (120 BPM if it has none), from the start of the song.
    double bpm = 120.0;

    for (int track = 0; track < midi.getNumTracks(); ++track)
        for (const auto* event : *midi.getTrack (track))
            if (event->message.isTempoMetaEvent())
            {
                bpm = 60.0 / event->message.getTempoSecondsPerQuarterNote();
                track = midi.getNumTracks() - 1;
                break;
            }

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

    const auto output = render ({ .sampleRate = sampleRate,
                                  .numSamples = lastEvent + (int) (tailSeconds * sampleRate),
                                  .events = events,
                                  .setUp = [presetIndex, overrides] (juce::AudioProcessor& p)
                                  {
                                      dynamic_cast<boringsynth::BoringSynthProcessor&> (p).getPresetManager().loadFactoryPreset (presetIndex);

                                      for (const auto& change : overrides)
                                          setParameter (p, change.upToFirstOccurrenceOf ("=", false, false),
                                                        change.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
                                  },
                                  .transport = [bpm] (int blockStart)
                                  {
                                      juce::AudioPlayHead::PositionInfo position;
                                      position.setBpm (bpm);
                                      position.setPpqPosition (blockStart / sampleRate * bpm / 60.0);
                                      position.setIsPlaying (true);
                                      return position;
                                  } });

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

    std::printf ("rendered %d MIDI events at %.2f BPM with \"%s\"%s%s to %s (%.3f s)\n",
                 (int) events.size(), bpm, presetName, overrides.isEmpty() ? "" : ", changed: ", overrides.joinIntoString (" ").toRawUTF8(),
                 wavFile.getFullPathName().toRawUTF8(), output.size() / sampleRate);
    return 0;
}

} // namespace testing
