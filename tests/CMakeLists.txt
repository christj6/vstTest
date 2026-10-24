# The plug-in's test program. RenderTests.cpp renders audio through the processor and checks
# the output, ParameterTests.cpp checks parameters and saved state, and OscillatorTests.cpp checks
# the oscillator on its own. New kinds of tests go in new files here, with a run function declared
# in TestHelpers.h and called from Main.cpp.
# Run with: ctest --test-dir build -C Release --output-on-failure
juce_add_console_app(Tests
    PRODUCT_NAME "Tests")

target_sources(Tests PRIVATE
    Main.cpp
    RenderTests.cpp
    ParameterTests.cpp
    OscillatorTests.cpp
    ReferenceRender.cpp
    ${BORINGSYNTH_SOURCES})

# The oscillator tests use the plug-in's classes directly.
target_include_directories(Tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(Tests PRIVATE
    ${BORINGSYNTH_COMPILE_DEFINITIONS}
    # juce_add_plugin defines this for the plug-in; the processor's getName() needs it here too.
    JucePlugin_Name="${BORINGSYNTH_PRODUCT_NAME}")

target_link_libraries(Tests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME Tests COMMAND Tests)
