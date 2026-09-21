cmake_minimum_required(VERSION 3.22)

# Set global configurations before fetching JUCE
set(JUCE_COPY_PLUGIN_AFTER_BUILD OFF CACHE BOOL "Disable copying plugin to system folders after build")

project(TunerBPMPlugin VERSION 2.2.0 LANGUAGES CXX)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

message(STATUS "Fetching JUCE...")
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce)

# Create the Audio Plugin
juce_add_plugin(TunerBPMPlugin
    COMPANY_NAME "vevikils"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_AFTER_BUILD OFF
    PLUGIN_NAME "Supreme Tuner BPM V.2.2"
    PRODUCT_NAME "Supreme Tuner BPM V.2.2"
    PLUGIN_CODE "Stb4"
    PLUGIN_MANUFACTURER_CODE "Vevi"
    VERSION "2.2.0"
    FORMATS VST3 Standalone
)

juce_generate_juce_header(TunerBPMPlugin)


# Set the plugin code files
target_sources(TunerBPMPlugin PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

# Link necessary modules
target_link_libraries(TunerBPMPlugin PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(TunerBPMPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
)

# Automated DSP Verification Target
juce_add_console_app(DSPTest PRODUCT_NAME "DSPTest")
juce_generate_juce_header(DSPTest)
target_sources(DSPTest PRIVATE
    Source/DSPTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_link_libraries(DSPTest PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)
target_compile_definitions(DSPTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="SupremeTunerBPMV22"
)

