cmake_minimum_required(VERSION 3.22)
project(FRAGATIME VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(FRAGATIME
    COMPANY_NAME "TapeLab"
    PLUGIN_MANUFACTURER_CODE Tlab
    PLUGIN_CODE Frag
    FORMATS VST3 Standalone
    PRODUCT_NAME "FRAGATIME"
    VERSION 1.1.0
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(FRAGATIME)

target_sources(FRAGATIME PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/GranularEngine.cpp
    Source/Analysis.cpp
    Source/TabieModel.cpp)

# JUCE_USE_MP3AUDIOFORMAT=1 permite abrir archivos .mp3
target_compile_definitions(FRAGATIME PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(FRAGATIME
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
