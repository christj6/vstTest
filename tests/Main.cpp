#include "TestHelpers.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testing::runRenderTests();
    testing::runParameterTests();

    if (testing::failures == 0)
        std::printf ("ALL PASSED\n");
    else
        std::printf ("%d FAILED\n", testing::failures);

    return testing::failures;
}
