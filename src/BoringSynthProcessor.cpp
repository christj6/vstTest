#include <juce_audio_utils/juce_audio_utils.h>

// A synth that does nothing but play a sine wave at the pitch of each held key.
// No envelope and no editor: note on starts the sine, note off stops it (with a 5 ms fade so the
// cut doesn't click). The only parameter is the output volume.

namespace
{
constexpr int numVoices = 16;

// Fixed output level per note. Eight notes peaking together reach 0.8, below full scale.
constexpr float level = 0.1f;

// Length of the fade-out on note off. Short enough to sound instant, long enough not to click.
constexpr double releaseSeconds = 0.005;

// Parameter and state identifiers are saved in DAW projects. Changing one breaks every project
// that uses it, so treat them as permanent.
constexpr const char* volumeId = "volume";
constexpr const char* stateTag = "Parameters";
constexpr const char* stateVersionId = "stateVersion";

// Bump this when the saved state changes in a way older versions need converting from.
constexpr int currentStateVersion = 1;

// Volume range in decibels. The minimum means silence.
constexpr float minVolumeDb = -60.0f;
constexpr float maxVolumeDb = 6.0f;

// How long a volume change takes to glide to its new value. Jumping instantly makes fast knob
// moves crackle ("zipper noise").
constexpr double volumeSmoothingSeconds = 0.02;

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
        const auto sampleRate = getSampleRate();
        const auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNote);

        // Stay silent if the host hasn't given us a sample rate yet, or if the note is at or
        // above the Nyquist frequency and would alias. This also keeps phaseDelta below pi,
        // so wrapping the phase once per sample is always enough.
        if (sampleRate <= 0.0 || frequency >= 0.5 * sampleRate)
        {
            clearCurrentNote();
            phaseDelta = 0.0;
            return;
        }

        phase = 0.0;
        phaseDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        gain = 1.0f;
        gainStep = 0.0f;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            gainStep = (float) (1.0 / (releaseSeconds * getSampleRate()));
            return;
        }

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
            const auto sample = gain * level * (float) std::sin (phase);
            phase += phaseDelta;

            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample (ch, i, sample);

            gain -= gainStep;

            if (gain <= 0.0f)
            {
                clearCurrentNote();
                phaseDelta = 0.0;
                break;
            }
        }
    }

private:
    double phase = 0.0;
    double phaseDelta = 0.0;
    float gain = 0.0f;
    float gainStep = 0.0f;
};

// JUCE's Synthesiser handles the sustain, sostenuto and soft pedals by default.
// This plug-in ignores them: a note plays exactly as long as its key is held.
class SineSynth  : public juce::Synthesiser
{
public:
    void handleSustainPedal (int, bool) override {}
    void handleSostenutoPedal (int, bool) override {}
    void handleSoftPedal (int, bool) override {}
};

class BoringSynthProcessor  : public juce::AudioProcessor
{
public:
    BoringSynthProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < numVoices; ++i)
            synth.addVoice (new SineVoice());

        synth.addSound (new SineSound());

        // By default the synth handles MIDI events up to 31 samples early to avoid tiny
        // render chunks. Rendering in chunks of any size keeps note timing sample-accurate.
        synth.setMinimumRenderingSubdivisionSize (1);
    }

    void prepareToPlay (double sampleRate, int) override
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);

        // Start at the current volume rather than gliding to it, so a project's saved volume
        // doesn't fade in when playback starts.
        volumeGain.reset (sampleRate, volumeSmoothingSeconds);
        volumeGain.setCurrentAndTargetValue (getVolumeGain());
    }
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
        volumeGain.setTargetValue (getVolumeGain());
        volumeGain.applyGain (buffer, buffer.getNumSamples());
        protectOutput (buffer);
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return releaseSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        auto state = parameters.copyState();
        state.setProperty (stateVersionId, currentStateVersion, nullptr);

        if (const auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        // Projects saved before the plug-in had parameters hold no state, and anything that isn't
        // our state is ignored. Either way the parameters keep their current values.
        const auto xml = getXmlFromBinary (data, sizeInBytes);

        if (xml == nullptr || ! xml->hasTagName (stateTag))
            return;

        parameters.replaceState (juce::ValueTree::fromXml (*xml));
    }

private:
    SineSynth synth;
    juce::AudioProcessorValueTreeState parameters { *this, nullptr, stateTag, createParameterLayout() };
    std::atomic<float>* volumeDb = parameters.getRawParameterValue (volumeId);

    // Smoothed linearly in gain rather than in dB, because the minimum volume is a gain of
    // exactly 0, which multiplicative (dB-style) smoothing can never reach.
    juce::SmoothedValue<float> volumeGain;

    float getVolumeGain() const
    {
        return juce::Decibels::decibelsToGain (volumeDb->load(), minVolumeDb);
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        auto volumeToText = [] (float value, int)
        {
            return value <= minVolumeDb ? juce::String ("-inf") : juce::String (value, 1);
        };

        auto textToVolume = [] (const juce::String& text)
        {
            return text.trim().startsWithIgnoreCase ("-inf") ? minVolumeDb : text.getFloatValue();
        };

        return { std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { volumeId, 1 },
                                                              "Volume",
                                                              juce::NormalisableRange<float> (minVolumeDb, maxVolumeDb, 0.1f),
                                                              0.0f,
                                                              juce::AudioParameterFloatAttributes()
                                                                  .withLabel ("dB")
                                                                  .withStringFromValueFunction (volumeToText)
                                                                  .withValueFromStringFunction (textToVolume)) };
    }

    // Last line of defence before audio leaves the plug-in. NaN or infinity would poison every
    // effect after us, so it becomes silence; anything past full scale is clipped to it.
    static void protectOutput (juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                data[i] = std::isfinite (data[i]) ? juce::jlimit (-1.0f, 1.0f, data[i]) : 0.0f;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoringSynthProcessor)
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BoringSynthProcessor();
}
