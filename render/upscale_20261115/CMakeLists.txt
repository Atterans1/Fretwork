cmake_minimum_required (VERSION 3.22)
project (UpscaleRender VERSION 1.0.0 LANGUAGES C CXX)
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

# Pulls in the Fretwork root for FretworkCore and JUCE. Only the harness targets below are built.
add_subdirectory ("C:/Users/jeffr/Fretwork" "${CMAKE_BINARY_DIR}/fretwork")

juce_add_console_app (ScaleRender PRODUCT_NAME "ScaleRender")
target_sources (ScaleRender PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ScaleRender.cpp")
target_link_libraries (ScaleRender PRIVATE FretworkCore juce::juce_audio_formats juce::juce_audio_basics juce::juce_recommended_config_flags)

juce_add_console_app (IsoRender PRODUCT_NAME "IsoRender")
target_sources (IsoRender PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/IsoRender.cpp")
target_link_libraries (IsoRender PRIVATE FretworkCore juce::juce_audio_formats juce::juce_audio_basics juce::juce_recommended_config_flags)

juce_add_console_app (LoopProbe PRODUCT_NAME "LoopProbe")
target_sources (LoopProbe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/LoopProbe.cpp")
target_link_libraries (LoopProbe PRIVATE FretworkCore juce::juce_recommended_config_flags)

juce_add_console_app (StringPitch PRODUCT_NAME "StringPitch")
target_sources (StringPitch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/StringPitch.cpp")
target_link_libraries (StringPitch PRIVATE FretworkCore juce::juce_recommended_config_flags)

juce_add_console_app (ProcRender PRODUCT_NAME "ProcRender")
target_sources (ProcRender PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ProcRender.cpp")
file (GLOB PROC_UI_SOURCES "C:/Users/jeffr/Fretwork/Source/ui/*.cpp")
target_sources (ProcRender PRIVATE "C:/Users/jeffr/Fretwork/Source/PluginEditor.cpp" ${PROC_UI_SOURCES})
target_include_directories (ProcRender PRIVATE "C:/Users/jeffr/Fretwork/Source" "C:/Users/jeffr/Fretwork/Source/ui")
target_link_libraries (ProcRender PRIVATE FretworkCore FretworkAssets juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics juce::juce_recommended_config_flags)

