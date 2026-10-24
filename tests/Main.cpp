#include "TestHelpers.h"

// With no arguments, runs every test. With --record <file> or --compare <file>, records or
// compares the reference render instead (see ReferenceRender.cpp).
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 3 && juce::String (argv[1]) == "--record")
        return testing::recordReference (argv[2]);

    if (argc == 3 && juce::String (argv[1]) == "--compare")
        return testing::compareReference (argv[2]);

    if (argc != 1)
    {
        std::printf ("usage: Tests [--record <file> | --compare <file>]\n");
        return 1;
    }

    testing::runRenderTests();
    testing::runParameterTests();
    testing::runOscillatorTests();

    if (testing::failures == 0)
        std::printf ("ALL PASSED\n");
    else
        std::printf ("%d FAILED\n", testing::failures);

    return testing::failures;
}
