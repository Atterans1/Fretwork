cmake_minimum_required(VERSION 3.22)

project(Fretwork VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(JUCE)

# AU is Apple-only; AAX needs Avid's SDK and signing and is not built here.
set(FRETWORK_FORMATS VST3 Standalone)
if (APPLE)
    list(APPEND FRETWORK_FORMATS AU)
endif()

juce_add_plugin(Fretwork
    COMPANY_NAME             "Jeffr"
    BUNDLE_ID                com.jeffr.fretwork
    PLUGIN_MANUFACTURER_CODE Jfry
    PLUGIN_CODE              Frtw
    FORMATS                  ${FRETWORK_FORMATS}
    PRODUCT_NAME             "Fretwork"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    VST3_CATEGORIES          "Instrument Synth"
    COPY_PLUGIN_AFTER_BUILD  TRUE)

# Guitar body art (AI-generated, cut out on black; see Source/ui/GuitarArt.h).
# The panel texture is still drawn (WavesStyle::chassis).
juce_add_binary_data(FretworkAssets SOURCES
    Assets/guitars/dreadnought.png
    Assets/guitars/classical.png
    Assets/guitars/electric.png
    Assets/guitars/semihollow.png)

# The DSP and performance cores are slice-developed, so glob them with
# CONFIGURE_DEPENDS: newly added .cpp files are picked up on the next build.
file(GLOB_RECURSE FRETWORK_DSP_SOURCES    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/*.cpp")
file(GLOB_RECURSE FRETWORK_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/*.cpp")
file(GLOB_RECURSE FRETWORK_UI_SOURCES     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/*.cpp")

add_library(FretworkCore STATIC
    Source/Params.cpp
    Source/SeqStore.cpp
    Source/PatchManager.cpp
    Source/SeqImport.cpp
    Source/MidiLearn.cpp
    Source/PluginProcessor.cpp
    ${FRETWORK_DSP_SOURCES}
    ${FRETWORK_ENGINE_SOURCES})

# JUCE draws UTF-8 label glyphs; without this MSVC decodes narrow literals as
# the system codepage and the transport buttons render as "a?" mojibake.
if (MSVC)
    target_compile_options(FretworkCore PUBLIC /utf-8)
    target_compile_options(Fretwork PRIVATE /utf-8)
endif()

target_include_directories(FretworkCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")

# The editor components include each other by bare filename, so Source/ui has to
# be resolvable for whichever target compiles them. It is deliberately PRIVATE to
# those two targets rather than PUBLIC on FretworkCore: a PUBLIC entry would put
# Source/ui on the include path of every consumer, letting a bare
# #include "FretboardView.h" from anywhere silently resolve.
target_include_directories(Fretwork PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui")

target_compile_definitions(FretworkCore PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(FretworkCore
    PUBLIC
        juce::juce_audio_utils
        juce::juce_dsp
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_sources(Fretwork PRIVATE
    Source/PluginMain.cpp
    Source/PluginEditor.cpp
    ${FRETWORK_UI_SOURCES})

target_link_libraries(Fretwork
    PRIVATE
        FretworkCore
        FretworkAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------- unit tests
enable_testing()

juce_add_console_app(FretworkTests PRODUCT_NAME "FretworkTests")

file(GLOB FRETWORK_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp")

target_include_directories(FretworkTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui")

if (MSVC)
    target_compile_options(FretworkTests PRIVATE /utf-8)
endif()

target_sources(FretworkTests PRIVATE
    ${FRETWORK_TEST_SOURCES}
    # PluginProcessor's vtable references createEditor, so the editor has to be
    # part of the test binary.
    Source/PluginEditor.cpp
    ${FRETWORK_UI_SOURCES})

# The test binary stack-allocates GuitarEngine directly; widening per-string
# storage to 7 courses pushed an already-large inline array past the default
# 1 MB reserve. 16 MB keeps headroom for future storage growth.
if (MSVC)
    target_link_options(FretworkTests PRIVATE "/STACK:16777216")
endif()

target_link_libraries(FretworkTests
    PRIVATE
        FretworkCore
        FretworkAssets
        juce::juce_recommended_config_flags)

add_test(NAME FretworkTests COMMAND FretworkTests)


# HostCheck loads the deployed Fretwork.vst3 through JUCE's VST3 host (the
# route a DAW takes) and checks automation, presets, editor scaling and MIDI.
# Windows-only: captures the editor through its HWND.
if (WIN32)
    juce_add_console_app(HostCheck PRODUCT_NAME "HostCheck")
    target_sources(HostCheck PRIVATE Tools/HostCheck.cpp)
    target_compile_definitions(HostCheck PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(HostCheck PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()

# PluginE2E: learns a real recording (MP3/WAV/FLAC + an independent tab or
# MIDI reference), plays the learned notes through ANY VST3 instrument via
# JUCE's VST3 host, and verifies the playback against the recording.
# Usage: Tools/PluginE2E/README.md
juce_add_console_app(PluginE2E PRODUCT_NAME "PluginE2E")
target_sources(PluginE2E PRIVATE
    Tools/PluginE2E/PluginE2E.cpp
    Tools/PluginE2E/Audio.cpp
    Tools/PluginE2E/Learner.cpp
    Tools/PluginE2E/Verifier.cpp)
if (MSVC)
    target_compile_options(PluginE2E PRIVATE /utf-8)
endif()
target_compile_definitions(PluginE2E PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(PluginE2E PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

# ModeMatrix: plays every mode and factory preset through a real build and
# scores the audio (VST3 hosted directly; Standalone via a loopback recording).
juce_add_console_app(ModeMatrix PRODUCT_NAME "ModeMatrix")
target_sources(ModeMatrix PRIVATE Tools/ModeMatrix.cpp)
target_compile_definitions(ModeMatrix PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ModeMatrix PRIVATE
    juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics
    juce::juce_recommended_config_flags)

# Windows-only helpers for driving the Standalone: WASAPI loopback capture and
# a MIDI sender for a loopMIDI port.
if (WIN32)
    add_executable(loopcap Tools/loopcap.cpp)
    add_executable(midisend Tools/midisend.cpp)
endif()

# Renders the plugin and writes the acoustic features a listener judges by, as
# JSON for a model to score. See Docs/EVALUATION.md.
juce_add_console_app(SoundJudge PRODUCT_NAME "SoundJudge")
# PluginProcessor's vtable references createEditor, so the editor sources
# have to be in the link, exactly as FretworkTests does it.
target_sources(SoundJudge PRIVATE
    Tools/SoundJudge.cpp
    Source/PluginEditor.cpp
    ${FRETWORK_UI_SOURCES})
target_include_directories(SoundJudge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui")
target_link_libraries(SoundJudge PRIVATE
    FretworkCore
    FretworkAssets
    juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics
    juce::juce_recommended_config_flags)
