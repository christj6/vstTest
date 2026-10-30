#include "TestHelpers.h"

// With no arguments, runs every test. With --record <file> or --compare <file> [tolerance in dBFS],
// records or compares the reference render instead (see ReferenceRender.cpp). With --benchmark, measures the
// plug-in's speed instead (see Benchmark.cpp). With --snapshot <file> [display scale], draws the
// plug-in's window to a PNG file, to look at without a DAW, as a display at that scale (2 for 200 %)
// would show it (see EditorTests.cpp). With --render <midi file> <wav file>
// <preset> [parameterId=value ...], plays a MIDI file through the plug-in with a factory preset,
// changed by any overrides (see RenderTool.cpp).
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 3 && juce::String (argv[1]) == "--record")
        return testing::recordReference (argv[2]);

    if ((argc == 3 || argc == 4) && juce::String (argv[1]) == "--compare")
        return testing::compareReference (argv[2], argc == 4 ? std::optional<double> (juce::String (argv[3]).getDoubleValue())
                                                              : std::nullopt);

    if (argc == 2 && juce::String (argv[1]) == "--benchmark")
        return testing::runBenchmark();

    if ((argc == 3 || argc == 4) && juce::String (argv[1]) == "--snapshot")
        return testing::saveEditorSnapshot (argv[2], argc == 4 ? juce::String (argv[3]).getFloatValue() : 1.0f);

    if (argc >= 5 && juce::String (argv[1]) == "--render")
    {
        juce::StringArray overrides;

        for (int i = 5; i < argc; ++i)
            overrides.add (argv[i]);

        return testing::renderMidiFile (argv[2], argv[3], argv[4], overrides);
    }

    if (argc != 1)
    {
        std::printf ("usage: Tests [--record <file> | --compare <file> [tolerance in dBFS] | --benchmark\n"
                     "              | --snapshot <file> [display scale]\n"
                     "              | --render <midi file> <wav file> <preset> [parameterId=value ...]]\n");
        return 1;
    }

    testing::runNoteTests();
    testing::runControlTests();
    testing::runSoundTests();
    testing::runLfoTests();
    testing::runParameterTests();
    testing::runEditorTests();
    testing::runPresetTests();
    testing::runRenderToolTests();
    testing::runReferenceRenderTests();
    testing::runOscillatorTests();
    testing::runEnvelopeTests();
    testing::runFilterTests();

    if (testing::failures == 0)
        std::printf ("ALL PASSED\n");
    else
        std::printf ("%d FAILED\n", testing::failures);

    return testing::failures;
}
