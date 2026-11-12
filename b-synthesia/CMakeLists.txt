cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)  # universale
endif()

project(BSynthesia VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: copia condivisa in "Self Made Plugins/libs/JUCE", altrimenti la scarica.
set(SMP_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libs/JUCE")
if(EXISTS "${SMP_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${SMP_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(BSynthesia
    COMPANY_NAME "b-suite"
    BUNDLE_ID com.bsuite.bsynthesia
    PLUGIN_MANUFACTURER_CODE Bsui
    PLUGIN_CODE Bsyn
    FORMATS AU VST3
    PRODUCT_NAME "b-synthesia"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD TRUE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

# Font IBM Plex Sans + Mono condivisi (SIL Open Font License, vedi ../shared/Resources/OFL-IBMPlex.txt)
juce_add_binary_data(BSynthesiaAssets SOURCES
    ../shared/Resources/IBMPlexSans-Light.ttf
    ../shared/Resources/IBMPlexSans-Regular.ttf
    ../shared/Resources/IBMPlexSans-Medium.ttf
    ../shared/Resources/IBMPlexSans-SemiBold.ttf
    ../shared/Resources/IBMPlexSans-Bold.ttf
    ../shared/Resources/IBMPlexMono-Regular.ttf
    ../shared/Resources/IBMPlexMono-Medium.ttf
    ../shared/Resources/IBMPlexMono-SemiBold.ttf)

set(BSYNTHESIA_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ../shared/smp/Widgets.cpp
    ../shared/smp/Shell.cpp)

target_sources(BSynthesia PRIVATE ${BSYNTHESIA_SOURCES})
target_include_directories(BSynthesia PRIVATE Source ../shared)
target_compile_definitions(BSynthesia PRIVATE SMP_ACCENT_ARGB=0xffe2e8f0)   
target_compile_definitions(BSynthesia PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(BSynthesia
    PRIVATE BSynthesiaAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- Test (riga di comando)
juce_add_console_app(BSynthesiaTests PRODUCT_NAME "BSynthesiaTests")
target_sources(BSynthesiaTests PRIVATE Tests/Tests.cpp ${BSYNTHESIA_SOURCES})
target_include_directories(BSynthesiaTests PRIVATE Source ../shared)
target_compile_definitions(BSynthesiaTests PRIVATE SMP_ACCENT_ARGB=0xffe2e8f0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_compile_definitions(BSynthesiaTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(BSynthesiaTests PRIVATE BSynthesiaAssets juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
