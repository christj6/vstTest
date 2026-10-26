cmake_minimum_required(VERSION 3.22)

project(BoringSynth VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(libs/JUCE)

# Shared by the plug-in and the test program (tests/CMakeLists.txt), so the two can't drift apart.
set(BORINGSYNTH_PRODUCT_NAME "BoringSynth")
set(BORINGSYNTH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BandLimitedWaveTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BandLimitedWaveTable.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BoringSynthEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BoringSynthEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BoringSynthProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BoringSynthProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Envelope.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Filter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Lfo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Oscillator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Parameters.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Synth.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Synth.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SynthVoice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SynthVoice.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WaveTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WaveTable.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Waveforms.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Waveforms.h)
set(BORINGSYNTH_COMPILE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    DONT_SET_USING_JUCE_NAMESPACE=1)

juce_add_plugin(BoringSynth
    COMPANY_NAME "Jack"
    PLUGIN_MANUFACTURER_CODE Jack
    PLUGIN_CODE Brng
    PRODUCT_NAME "${BORINGSYNTH_PRODUCT_NAME}"
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Copying into Program Files needs admin; point a junction at the build output instead.
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(BoringSynth PRIVATE ${BORINGSYNTH_SOURCES})

target_compile_definitions(BoringSynth PUBLIC ${BORINGSYNTH_COMPILE_DEFINITIONS})

target_link_libraries(BoringSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_subdirectory(tests)
