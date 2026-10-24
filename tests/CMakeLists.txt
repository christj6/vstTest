# Renders audio through the plug-in's processor and checks the output.
# Run with: ctest --test-dir build -C Release --output-on-failure
juce_add_console_app(RenderTests
    PRODUCT_NAME "RenderTests")

target_sources(RenderTests PRIVATE
    RenderTests.cpp
    ../src/BoringSynthProcessor.cpp)

target_compile_definitions(RenderTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    DONT_SET_USING_JUCE_NAMESPACE=1
    # juce_add_plugin defines this for the plug-in; the processor's getName() needs it here too.
    JucePlugin_Name="BoringSynth")

target_link_libraries(RenderTests
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME RenderTests COMMAND RenderTests)
