# The plug-in's test program. NoteTests.cpp, ControlTests.cpp, SoundTests.cpp and LfoTests.cpp
# render audio through the whole plug-in and check the output; ParameterTests.cpp checks parameters
# and saved state, and EditorTests.cpp the plug-in's window; OscillatorTests.cpp, EnvelopeTests.cpp
# and FilterTests.cpp check those classes on their own.
# ReferenceRender.cpp is the --record / --compare check, and Benchmark.cpp is --benchmark.
# Shared helpers are in TestHelpers.h and Measurements.h. New kinds of tests go in new files here,
# with a run function declared in TestHelpers.h and called from Main.cpp.
# Run with: ctest --test-dir build -C Release --output-on-failure
juce_add_console_app(Tests
    PRODUCT_NAME "Tests")

target_sources(Tests PRIVATE
    Main.cpp
    NoteTests.cpp
    ControlTests.cpp
    SoundTests.cpp
    LfoTests.cpp
    ParameterTests.cpp
    EditorTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    FilterTests.cpp
    ReferenceRender.cpp
    Benchmark.cpp
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
