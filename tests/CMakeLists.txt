# The plug-in's test program. RenderTests.cpp renders audio through the processor and checks
# the output, ParameterTests.cpp checks parameters and saved state. New kinds of tests go in new
# files here, with a run function declared in TestHelpers.h and called from Main.cpp.
# Run with: ctest --test-dir build -C Release --output-on-failure
juce_add_console_app(Tests
    PRODUCT_NAME "Tests")

target_sources(Tests PRIVATE
    Main.cpp
    RenderTests.cpp
    ParameterTests.cpp
    ../src/BoringSynthProcessor.cpp)

target_compile_definitions(Tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    DONT_SET_USING_JUCE_NAMESPACE=1
    # juce_add_plugin defines this for the plug-in; the processor's getName() needs it here too.
    JucePlugin_Name="BoringSynth")

target_link_libraries(Tests
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME Tests COMMAND Tests)
