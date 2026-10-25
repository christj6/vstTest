#include "TestHelpers.h"

// With no arguments, runs every test. With --record <file> or --compare <file>, records or
// compares the reference render instead (see ReferenceRender.cpp). With --benchmark, measures the
// plug-in's speed instead (see Benchmark.cpp).
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 3 && juce::String (argv[1]) == "--record")
        return testing::recordReference (argv[2]);

    if (argc == 3 && juce::String (argv[1]) == "--compare")
        return testing::compareReference (argv[2]);

    if (argc == 2 && juce::String (argv[1]) == "--benchmark")
        return testing::runBenchmark();

    if (argc != 1)
    {
        std::printf ("usage: Tests [--record <file> | --compare <file> | --benchmark]\n");
        return 1;
    }

    testing::runNoteTests();
    testing::runControlTests();
    testing::runSoundTests();
    testing::runLfoTests();
    testing::runParameterTests();
    testing::runOscillatorTests();
    testing::runEnvelopeTests();
    testing::runFilterTests();

    if (testing::failures == 0)
        std::printf ("ALL PASSED\n");
    else
        std::printf ("%d FAILED\n", testing::failures);

    return testing::failures;
}
