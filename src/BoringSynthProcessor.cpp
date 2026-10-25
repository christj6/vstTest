#include "BoringSynthProcessor.h"

#include "SynthVoice.h"

namespace boringsynth
{

namespace
{

constexpr int numVoices = 16;

// How long a volume change takes to glide to its new value. Jumping instantly makes fast knob
// moves crackle ("zipper noise").
constexpr double volumeSmoothingSeconds = 0.02;

// Last line of defence before audio leaves the plug-in. NaN or infinity would poison every
// effect after us, so it becomes silence; anything past full scale is clipped to it.
void protectOutput (juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = std::isfinite (data[i]) ? juce::jlimit (-1.0f, 1.0f, data[i]) : 0.0f;
    }
}

} // namespace

BoringSynthProcessor::BoringSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    const VoiceParameters voiceParameters { *velocitySensitivity, *pitchBendRange, *waveform,
                                            *ampAttack, *ampDecay, *ampSustain, *ampRelease,
                                            *filterEnabled, *filterCutoff, *filterResonance, *filterType };

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice (waveformTables, voiceParameters));

    synth.addSound (new SynthSound());

    // By default the synth handles MIDI events up to 31 samples early to avoid tiny
    // render chunks. Rendering in chunks of any size keeps note timing sample-accurate.
    synth.setMinimumRenderingSubdivisionSize (1);
}

void BoringSynthProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);

    // Start at the current volume rather than gliding to it, so a project's saved volume
    // doesn't fade in when playback starts.
    volumeGain.reset (sampleRate, volumeSmoothingSeconds);
    volumeGain.setCurrentAndTargetValue (getVolumeGain());
}

bool BoringSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void BoringSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
    volumeGain.setTargetValue (getVolumeGain());
    volumeGain.applyGain (buffer, buffer.getNumSamples());
    protectOutput (buffer);
}

double BoringSynthProcessor::getTailLengthSeconds() const
{
    // How long notes keep sounding after their keys are released.
    return ampRelease->load();
}

void BoringSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionId, currentStateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BoringSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Projects saved before the plug-in had parameters hold no state, and anything that isn't
    // our state is ignored. Either way the parameters keep their current values.
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (stateTag))
        return;

    parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

float BoringSynthProcessor::getVolumeGain() const
{
    return juce::Decibels::decibelsToGain (volumeDb->load(), minVolumeDb);
}

} // namespace boringsynth

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new boringsynth::BoringSynthProcessor();
}
