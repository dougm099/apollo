# Apollo v2 — DNA-to-DNA sample matching (JUCE 9 + CMake)
#
# Configure + build (macOS):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# The AU/VST3 bundles are copied to ~/Library/Audio/Plug-Ins/ automatically
# (COPY_PLUGIN_AFTER_BUILD). Rescan plug-ins in the host to pick up changes.

cmake_minimum_required(VERSION 3.22)

# NOTE: these must be set BEFORE project() — CMake reads them when the
# project() call configures the toolchain. Setting them after project()
# is silently ignored and you get a native-arch-only build.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")

project(Apollo VERSION 0.2.0 LANGUAGES C CXX)

# --- Language / platform ----------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# macOS 11+ universal binary (Apple Silicon + Intel).

# --- JUCE -------------------------------------------------------------------
# Pinned to the latest stable release at time of writing (Sept 2026).
# Check https://github.com/juce-framework/JUCE/releases for newer tags.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.2
)
FetchContent_MakeAvailable(JUCE)

# --- Plugin target -----------------------------------------------------------
juce_add_plugin(Apollo
    COMPANY_NAME              "Apollo Audio"
    BUNDLE_ID                 com.apolloaudio.apollo
    PRODUCT_NAME              "Apollo"
    DESCRIPTION               "Captures the DNA of your track and matches it to samples, band by band"
    VERSION_STRING            "0.2.0"

    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    COPY_PLUGIN_AFTER_BUILD   TRUE

    PLUGIN_MANUFACTURER_CODE  Aplo   # 4-char manufacturer id — change to your own
    PLUGIN_CODE               Apl1   # 4-char plugin id — must be unique to this plugin

    FORMATS                   AU VST3 Standalone

    VST3_CATEGORIES           "Fx" "Analyzer"
    AU_MAIN_TYPE              "kAudioUnitType_Effect"
)

juce_generate_juce_header(Apollo)

target_sources(Apollo PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analysis/MatchAnalyzer.cpp
    Source/Analysis/DnaExtractor.cpp
    Source/Analysis/DnaLibrary.cpp
)

target_include_directories(Apollo PRIVATE Source)

target_compile_definitions(Apollo
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Apollo
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats   # WAV demo-library loading
        juce::juce_dsp             # FFT for DNA extraction
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
