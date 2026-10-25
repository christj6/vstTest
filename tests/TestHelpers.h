#pragma once

#include "Measurements.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdio>
#include <functional>
#include <vector>

// Shared by every test file. Checks print PASS or FAIL and count failures; the program's exit code
// is the number of failed checks, so CTest sees any failure.

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace testing
{

inline int failures = 0;

inline void check (bool passed, const char* description)
{
    std::printf ("%s  %s\n", passed ? "PASS" : "FAIL", description);

    if (! passed)
        ++failures;
}

struct TimedMessage
{
    int samplePosition;
    juce::MidiMessage message;
};

// The sample rate most tests render at (RenderOptions' default).
constexpr double testSampleRate = 48000.0;

inline juce::MidiMessage noteOn (int note, int velocity = 100)
{
    return juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity);
}

inline juce::MidiMessage noteOff (int note) { return juce::MidiMessage::noteOff (1, note); }

// One A4 note held for a second, the starting point for most of the level tests.
inline const std::vector<TimedMessage> heldNote { { 0, noteOn (69) } };

// A block start partway through a held note, where tests change something (a parameter, the pitch
// wheel) the way a host delivers automation: at the start of a block.
constexpr int changeSample = 47 * 512;

using ProcessorSetUp = std::function<void (juce::AudioProcessor&)>;
using BlockHook = std::function<void (juce::AudioProcessor&, int blockStart)>;

// What to render. Give only the fields a test cares about, by name, in this order:
//   render ({ .numSamples = 9600, .events = { { 0, noteOn (69) } } })
struct RenderOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    int numSamples = 48000;

    // MIDI at absolute sample positions, delivered in whichever block it falls in, as a host would.
    std::vector<TimedMessage> events;

    // Whether to call prepareToPlay first. Without it the plug-in has no sample rate.
    bool prepare = true;

    // Runs on the new processor before any audio is rendered, e.g. to change parameters.
    ProcessorSetUp setUp;

    // Runs before each block with the block's first sample position, e.g. to automate a parameter
    // mid-render the way a host does.
    BlockHook beforeEachBlock;
};

// Renders channel 0 of the plug-in's output.
inline std::vector<float> render (const RenderOptions& options)
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());

    if (options.setUp)
        options.setUp (*processor);

    if (options.prepare)
        processor->prepareToPlay (options.sampleRate, options.blockSize);

    std::vector<float> output;
    juce::AudioBuffer<float> buffer (2, options.blockSize);

    for (int blockStart = 0; blockStart < options.numSamples; blockStart += options.blockSize)
    {
        const auto numThisBlock = std::min (options.blockSize, options.numSamples - blockStart);
        buffer.setSize (2, numThisBlock, false, false, true);

        juce::MidiBuffer midi;

        for (const auto& event : options.events)
            if (event.samplePosition >= blockStart && event.samplePosition < blockStart + numThisBlock)
                midi.addEvent (event.message, event.samplePosition - blockStart);

        if (options.beforeEachBlock)
            options.beforeEachBlock (*processor, blockStart);

        processor->processBlock (buffer, midi);

        for (int i = 0; i < numThisBlock; ++i)
            output.push_back (buffer.getSample (0, i));
    }

    return output;
}

inline juce::RangedAudioParameter* findParameter (juce::AudioProcessor& processor, const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (ranged->getParameterID() == id)
                return ranged;

    return nullptr;
}

// Sets a parameter in its own units (e.g. dB), the way a host automating it would.
inline void setParameter (juce::AudioProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = findParameter (processor, id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    else
        check (false, ("parameter '" + id + "' exists").toRawUTF8());
}

// Reads a parameter in its own units.
inline float getParameter (juce::AudioProcessor& processor, const juce::String& id)
{
    if (auto* parameter = findParameter (processor, id))
        return parameter->convertFrom0to1 (parameter->getValue());

    check (false, ("parameter '" + id + "' exists").toRawUTF8());
    return 0.0f;
}

void runNoteTests();
void runControlTests();
void runSoundTests();
void runParameterTests();
void runOscillatorTests();
void runEnvelopeTests();
void runFilterTests();

// The reference render (ReferenceRender.cpp). Each returns the program's exit code.
int recordReference (const char* path);
int compareReference (const char* path);

// The CPU benchmark (Benchmark.cpp). Returns the program's exit code.
int runBenchmark();

} // namespace testing
