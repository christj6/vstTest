#include "BoringSynthProcessor.h"

#include "BoringSynthEditor.h"
#include "SynthVoice.h"

namespace boringsynth
{

namespace
{

constexpr int numVoices = 16;

// How long a volume change takes to glide to its new value. Jumping instantly makes fast knob
// moves crackle ("zipper noise").
constexpr double volumeSmoothingSeconds = 0.02;

// The saved state's property for the editor window's size.
constexpr const char* editorScaleId = "editorScale";

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

// What the host's play head says about its transport, if it has one: some hosts leave out the tempo
// or the song position, and some (and the tests, unless they give one) have no play head at all.
Transport readTransport (juce::AudioPlayHead* playHead)
{
    Transport transport;

    if (playHead == nullptr)
        return transport;

    if (const auto position = playHead->getPosition())
    {
        if (const auto bpm = position->getBpm())
            transport.tempoBpm = *bpm;

        if (const auto quarterNotes = position->getPpqPosition())
            transport.songPositionQuarterNotes = *quarterNotes;

        transport.isPlaying = position->getIsPlaying();
    }

    return transport;
}

} // namespace

BoringSynthProcessor::BoringSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Named, so a parameter can't be connected to the wrong field: they must be in VoiceParameters'
    // order, and leaving one out doesn't compile.
    const VoiceParameters voiceParameters { .velocitySensitivity = *velocitySensitivity,
                                            .pitchBendRange = *pitchBendRange,
                                            .waveform = *waveform,
                                            .ampAttack = *ampAttack,
                                            .ampDecay = *ampDecay,
                                            .ampSustain = *ampSustain,
                                            .ampRelease = *ampRelease,
                                            .filterEnabled = *filterEnabled,
                                            .filterCutoff = *filterCutoff,
                                            .filterResonance = *filterResonance,
                                            .filterType = *filterType,
                                            .filterKeyTracking = *filterKeyTracking,
                                            .filterVelocity = *filterVelocity,
                                            .filterAttack = *filterAttack,
                                            .filterDecay = *filterDecay,
                                            .filterSustain = *filterSustain,
                                            .filterRelease = *filterRelease,
                                            .filterEnvAmount = *filterEnvAmount };

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice (waveformTables, synth.getModulation(), voiceParameters));

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

juce::AudioProcessorEditor* BoringSynthProcessor::createEditor()
{
    return new BoringSynthEditor (*this, parameters, presets, editorScale);
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
    synth.setTransport (readTransport (getPlayHead()));
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

int BoringSynthProcessor::getNumPrograms()
{
    return presets.getNumFactoryPresets();
}

int BoringSynthProcessor::getCurrentProgram()
{
    return std::max (0, presets.getCurrentFactoryPreset());
}

void BoringSynthProcessor::setCurrentProgram (int index)
{
    // Some hosts set the program again when they reopen a project, after restoring its settings.
    // Loading it then would throw away any changes made since choosing it, so choosing the program
    // that's already current does nothing.
    if (index != getCurrentProgram())
        presets.loadFactoryPreset (index);
}

const juce::String BoringSynthProcessor::getProgramName (int index)
{
    return presets.getFactoryPresetName (index);
}

void BoringSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionId, currentStateVersion, nullptr);
    presets.writeTo (state);
    state.setProperty (editorScaleId, editorScale.load(), nullptr);

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

    const auto state = juce::ValueTree::fromXml (*xml);
    parameters.replaceState (state);
    presets.readFrom (state);

    // Projects saved before the window could be resized open it at its normal size. An open window
    // keeps its size; the next one opens at this.
    editorScale = juce::jlimit (BoringSynthEditor::minScale, BoringSynthEditor::maxScale,
                                (float) state.getProperty (editorScaleId, 1.0f));

    // JUCE's on/off parameters keep whatever value a host last gave them: 0.22, say, which counts as
    // off. Restoring a state with one off then leaves 0.22 alone, as it already counts as off, and
    // the host reads back 0.22 rather than the 0 it saved. So set each one to exactly on or off.
    for (auto* parameter : getParameters())
        if (auto* toggle = dynamic_cast<juce::AudioParameterBool*> (parameter))
            if (const auto exact = toggle->get() ? 1.0f : 0.0f; parameter->getValue() != exact)
                parameter->setValueNotifyingHost (exact);
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
