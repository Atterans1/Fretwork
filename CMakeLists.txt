cmake_minimum_required(VERSION 3.22)

project(Fretwork VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(JUCE)

juce_add_plugin(Fretwork
    COMPANY_NAME             "Jeffr"
    BUNDLE_ID                com.jeffr.fretwork
    PLUGIN_MANUFACTURER_CODE Jfry
    PLUGIN_CODE              Frtw
    FORMATS                  VST3 Standalone
    PRODUCT_NAME             "Fretwork"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    VST3_CATEGORIES          "Instrument Synth"
    COPY_PLUGIN_AFTER_BUILD  TRUE)

# No BinaryData assets: the guitar art and the panel texture are drawn
# (Source/ui/GuitarArt.cpp, WavesStyle::chassis), so nothing to ship.

# The DSP and performance cores are slice-developed, so glob them with
# CONFIGURE_DEPENDS: newly added .cpp files are picked up on the next build.
file(GLOB_RECURSE FRETWORK_DSP_SOURCES    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/*.cpp")
file(GLOB_RECURSE FRETWORK_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/*.cpp")
file(GLOB_RECURSE FRETWORK_UI_SOURCES     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/*.cpp")

add_library(FretworkCore STATIC
    Source/Params.cpp
    Source/SeqStore.cpp
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

target_link_libraries(FretworkTests
    PRIVATE
        FretworkCore
        juce::juce_recommended_config_flags)

add_test(NAME FretworkTests COMMAND FretworkTests)

# HostCheck loads the deployed Fretwork.vst3 through JUCE's VST3 host (the
# route a DAW takes) and checks automation, presets, editor scaling and MIDI.
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
