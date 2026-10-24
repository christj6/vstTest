#include "TestHelpers.h"

// A fixed render covering everything the plug-in does, for checking that a change doesn't alter the
// sound (or alters it only as much as intended). Record it before a change, compare after:
//   Tests --record build/reference.bin
//   (make the change and rebuild)
//   Tests --compare build/reference.bin
// The file is a header followed by the raw samples. It's only meant to be compared on the machine
// and build that recorded it, so it isn't committed.

namespace testing
{

namespace
{

constexpr juce::uint32 fileTag = 0x46525342; // "BSRF"
constexpr juce::uint32 fileVersion = 1;

struct Section
{
    const char* name;
    RenderOptions options;
};

std::vector<Section> referenceSections()
{
    auto on = [] (int note, int velocity) { return juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity); };
    auto off = [] (int note) { return juce::MidiMessage::noteOff (1, note); };
    auto pedal = [] (int value) { return juce::MidiMessage::controllerEvent (1, 64, value); };
    auto wheel = [] (int value) { return juce::MidiMessage::pitchWheel (1, value); };

    // Chords, mixed velocities, bends up and down, the sustain pedal, and notes from very low to
    // very high, over three seconds at 48 kHz.
    const std::vector<TimedMessage> events { { 0, on (60, 100) }, { 0, on (64, 40) }, { 10, on (67, 127) },
                                             { 3000, wheel (14000) }, { 9000, wheel (2000) }, { 15000, wheel (8192) },
                                             { 20000, pedal (127) }, { 24000, off (60) }, { 24001, off (64) },
                                             { 30000, on (72, 90) }, { 30005, on (79, 10) }, { 36000, off (67) },
                                             { 40000, pedal (0) }, { 50000, on (96, 127) }, { 60000, off (72) },
                                             { 70000, off (79) }, { 80000, off (96) }, { 90000, on (30, 64) },
                                             { 110000, off (30) } };

    auto customised = [] (juce::AudioProcessor& p)
    {
        setParameter (p, "velocitySensitivity", 60.0f);
        setParameter (p, "pitchBendRange", 5.0f);
    };

    auto volumeAutomation = [] (juce::AudioProcessor& p, int blockStart)
    {
        if (blockStart == 64 * 700)
            setParameter (p, "volume", -9.5f);
    };

    return { { "default settings", { .numSamples = 144000, .events = events } },
             { "custom settings, volume automation", { .numSamples = 144000, .events = events,
                                                       .setUp = customised, .beforeEachBlock = volumeAutomation } },
             { "custom settings, 64-sample blocks", { .blockSize = 64, .numSamples = 144000, .events = events,
                                                      .setUp = customised, .beforeEachBlock = volumeAutomation } } };
}

juce::File fileFromArgument (const char* path)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

} // namespace

int recordReference (const char* path)
{
    const auto file = fileFromArgument (path);
    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (! stream.openedOk())
    {
        std::printf ("couldn't write %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto sections = referenceSections();
    stream.writeInt ((int) fileTag);
    stream.writeInt ((int) fileVersion);
    stream.writeInt ((int) sections.size());

    for (const auto& section : sections)
    {
        const auto output = render (section.options);
        stream.writeInt ((int) output.size());
        stream.write (output.data(), output.size() * sizeof (float));
    }

    std::printf ("recorded the reference render to %s\n", file.getFullPathName().toRawUTF8());
    return 0;
}

int compareReference (const char* path)
{
    const auto file = fileFromArgument (path);
    juce::FileInputStream stream (file);
    const auto sections = referenceSections();

    if (! stream.openedOk() || (juce::uint32) stream.readInt() != fileTag || (juce::uint32) stream.readInt() != fileVersion
        || stream.readInt() != (int) sections.size())
    {
        std::printf ("%s is missing or isn't a reference render from this version of the tests; record it again\n",
                     file.getFullPathName().toRawUTF8());
        return 1;
    }

    double largestOverall = 0.0;

    for (const auto& section : sections)
    {
        const auto output = render (section.options);
        std::vector<float> reference ((size_t) stream.readInt());

        if (reference.size() != output.size()
            || stream.read (reference.data(), (int) (reference.size() * sizeof (float))) != (int) (reference.size() * sizeof (float)))
        {
            std::printf ("%s doesn't match this version of the tests; record it again\n", file.getFullPathName().toRawUTF8());
            return 1;
        }

        double largest = 0.0;
        size_t largestAt = 0;

        for (size_t i = 0; i < output.size(); ++i)
        {
            const auto difference = std::abs ((double) output[i] - (double) reference[i]);

            if (difference > largest)
            {
                largest = difference;
                largestAt = i;
            }
        }

        if (largest == 0.0)
            std::printf ("%-36s identical\n", section.name);
        else
            std::printf ("%-36s largest difference %.3e (%.1f dBFS) at sample %zu\n",
                         section.name, largest, 20.0 * std::log10 (largest), largestAt);

        largestOverall = std::max (largestOverall, largest);
    }

    if (largestOverall == 0.0)
        std::printf ("the output is identical to the reference\n");
    else
        std::printf ("largest difference overall: %.3e (%.1f dBFS)\n", largestOverall, 20.0 * std::log10 (largestOverall));

    return 0;
}

} // namespace testing
