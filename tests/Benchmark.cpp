#include "TestHelpers.h"

// A quick CPU benchmark: how much faster than real time the plug-in runs, playing through the same
// processBlock a DAW calls. Run it from a Release build, before and after a change that could
// affect speed:
//   Tests --benchmark
// It only reports numbers; nothing passes or fails. They vary between computers and from run to
// run by a few percent, so compare runs on the same machine.

namespace testing
{

namespace
{

constexpr double benchmarkSampleRate = 48000.0;
constexpr int benchmarkBlockSize = 512;
constexpr double benchmarkSeconds = 30.0;
constexpr int benchmarkRepeats = 3;

struct Scenario
{
    const char* name;
    bool sixteenNotes;
    bool movePitchWheel;
    ProcessorSetUp setUp;
    BlockHook beforeEachBlock = {};
};

// Seconds taken to render benchmarkSeconds of audio.
double timeScenario (const Scenario& scenario)
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());

    if (scenario.setUp)
        scenario.setUp (*processor);

    processor->prepareToPlay (benchmarkSampleRate, benchmarkBlockSize);

    juce::AudioBuffer<float> buffer (2, benchmarkBlockSize);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);

    const auto numBlocks = (int) (benchmarkSeconds * benchmarkSampleRate / benchmarkBlockSize);
    const auto start = juce::Time::getMillisecondCounterHiRes();

    for (int block = 0; block < numBlocks; ++block)
    {
        midi.clear();

        // Sixteen notes three semitones apart, from C2 up to A5, held throughout: every voice busy.
        if (block == 0 && scenario.sixteenNotes)
            for (int i = 0; i < 16; ++i)
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + 3 * i, (juce::uint8) 100), 0);

        // Sweeping the wheel fully up and down every couple of seconds keeps every voice gliding in
        // pitch and moving between band-limited table levels.
        if (scenario.movePitchWheel)
        {
            const auto sweep = std::sin (juce::MathConstants<double>::twoPi * block / 200.0);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + juce::roundToInt (8191.0 * sweep)), 0);
        }

        if (scenario.beforeEachBlock)
            scenario.beforeEachBlock (*processor, block * benchmarkBlockSize);

        processor->processBlock (buffer, midi);
    }

    return (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
}

} // namespace

int runBenchmark()
{
   #if JUCE_DEBUG
    std::printf ("This is a Debug build, which runs far slower than the plug-in a DAW loads from a "
                 "Release build. Run the Release build of Tests for meaningful numbers.\n\n");
   #endif

    const std::vector<Scenario> scenarios {
        { "no notes playing", false, false, {} },
        { "16 notes, sine", true, false, {} },
        { "16 notes, saw, pitch wheel moving", true, true,
          [] (juce::AudioProcessor& p) { setParameter (p, "waveform", 2.0f); } },

        // A moving filter: its cutoff changes on every block, so every voice keeps working out its
        // filter coefficients again (every 8 samples).
        { "16 notes, saw, filter cutoff moving", true, false,
          [] (juce::AudioProcessor& p)
          {
              setParameter (p, "waveform", 2.0f);
              setParameter (p, "filterEnabled", 1.0f);
              setParameter (p, "filterResonance", 50.0f);
          },
          [] (juce::AudioProcessor& p, int blockStart)
          {
              const auto sweep = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * blockStart / 96000.0);
              setParameter (p, "filterCutoff", (float) (200.0 * std::pow (50.0, sweep)));
          } },

        // The filter envelope: every voice moves its own cutoff all the time.
        { "16 notes, saw, filter envelope", true, false,
          [] (juce::AudioProcessor& p)
          {
              setParameter (p, "waveform", 2.0f);
              setParameter (p, "filterEnabled", 1.0f);
              setParameter (p, "filterCutoff", 300.0f);
              setParameter (p, "filterResonance", 50.0f);
              setParameter (p, "filterEnvAmount", 4.0f);
              setParameter (p, "filterSustain", 30.0f);
          } } };

    const auto blockBudgetMs = 1000.0 * benchmarkBlockSize / benchmarkSampleRate;
    std::printf ("Rendering %.0f s of audio in %d-sample blocks at %.0f Hz, fastest of %d runs:\n\n",
                 benchmarkSeconds, benchmarkBlockSize, benchmarkSampleRate, benchmarkRepeats);

    for (const auto& scenario : scenarios)
    {
        auto fastest = std::numeric_limits<double>::max();

        for (int run = 0; run < benchmarkRepeats; ++run)
            fastest = std::min (fastest, timeScenario (scenario));

        const auto timesRealTime = benchmarkSeconds / fastest;
        const auto msPerBlock = 1000.0 * fastest / (benchmarkSeconds * benchmarkSampleRate / benchmarkBlockSize);
        std::printf ("  %-35s %8.1fx real time  (%5.2f %% of one CPU core, %.4f ms of the %.2f ms each block allows)\n",
                     scenario.name, timesRealTime, 100.0 / timesRealTime, msPerBlock, blockBudgetMs);
    }

    return 0;
}

} // namespace testing
