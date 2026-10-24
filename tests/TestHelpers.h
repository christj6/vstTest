#pragma once

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

using ProcessorSetUp = std::function<void (juce::AudioProcessor&)>;
using BlockHook = std::function<void (juce::AudioProcessor&, int blockStart)>;

// Renders channel 0 of the plug-in's output. Events are given at absolute sample positions and are
// delivered in whichever block they fall in, the way a host would. setUp, if given, runs on the
// new processor before any audio is rendered, e.g. to change parameters. beforeEachBlock, if
// given, runs before each block with the block's first sample position, e.g. to automate a
// parameter mid-render the way a host does.
inline std::vector<float> render (double rate, int blockSize, int numSamples,
                                  const std::vector<TimedMessage>& events,
                                  bool prepare = true, const ProcessorSetUp& setUp = {},
                                  const BlockHook& beforeEachBlock = {})
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());

    if (setUp)
        setUp (*processor);

    if (prepare)
        processor->prepareToPlay (rate, blockSize);

    std::vector<float> output;
    juce::AudioBuffer<float> buffer (2, blockSize);

    for (int blockStart = 0; blockStart < numSamples; blockStart += blockSize)
    {
        const auto numThisBlock = std::min (blockSize, numSamples - blockStart);
        buffer.setSize (2, numThisBlock, false, false, true);

        juce::MidiBuffer midi;

        for (const auto& event : events)
            if (event.samplePosition >= blockStart && event.samplePosition < blockStart + numThisBlock)
                midi.addEvent (event.message, event.samplePosition - blockStart);

        if (beforeEachBlock)
            beforeEachBlock (*processor, blockStart);

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

void runRenderTests();
void runParameterTests();
} // namespace testing
