#include <juce_audio_utils/juce_audio_utils.h>

// A synth that does nothing but play a sine wave at the pitch of each held key.
// No envelope, no parameters, no editor: note on starts the sine, note off stops it.

namespace
{
constexpr int numVoices = 16;
constexpr float level = 0.2f; // fixed output level per note, leaves headroom for chords

struct SineSound  : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SineVoice  : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SineSound*> (sound) != nullptr;
    }

    void startNote (int midiNote, float, juce::SynthesiserSound*, int) override
    {
        phase = 0.0;
        phaseDelta = juce::MathConstants<double>::twoPi
                     * juce::MidiMessage::getMidiNoteInHertz (midiNote) / getSampleRate();
    }

    void stopNote (float, bool) override
    {
        clearCurrentNote();
        phaseDelta = 0.0;
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        if (phaseDelta == 0.0)
            return;

        for (int i = startSample; i < startSample + numSamples; ++i)
        {
            const auto sample = level * (float) std::sin (phase);
            phase += phaseDelta;

            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample (ch, i, sample);
        }
    }

private:
    double phase = 0.0;
    double phaseDelta = 0.0;
};

class TestProcessor  : public juce::AudioProcessor
{
public:
    TestProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < numVoices; ++i)
            synth.addVoice (new SineVoice());
        synth.addSound (new SineSound());
    }

    void prepareToPlay (double sampleRate, int) override { synth.setCurrentPlaybackSampleRate (sampleRate); }
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    juce::Synthesiser synth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestProcessor)
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestProcessor();
}
