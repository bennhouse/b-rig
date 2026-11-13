cmake_minimum_required(VERSION 3.22)

# versione della suite, architetture macOS, opzioni del compilatore e JUCE: comuni a tutti i plugin
include("${CMAKE_CURRENT_LIST_DIR}/../shared/cmake/Suite.cmake")
project(BSpace VERSION ${BSUITE_VERSION} LANGUAGES C CXX)
bsuite_setup()

juce_add_plugin(BSpace
    COMPANY_NAME "b-suite"
    BUNDLE_ID com.bsuite.bspace
    PLUGIN_MANUFACTURER_CODE Bsui
    PLUGIN_CODE Bspc
    FORMATS AU VST3
    PRODUCT_NAME "b-space"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    COPY_PLUGIN_AFTER_BUILD ${BSUITE_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

# Font IBM Plex Sans + Mono condivisi (SIL Open Font License, vedi ../shared/Resources/OFL-IBMPlex.txt)
juce_add_binary_data(BSpaceAssets SOURCES
    ../shared/Resources/IBMPlexSans-Light.ttf
    ../shared/Resources/IBMPlexSans-Regular.ttf
    ../shared/Resources/IBMPlexSans-Medium.ttf
    ../shared/Resources/IBMPlexSans-SemiBold.ttf
    ../shared/Resources/IBMPlexSans-Bold.ttf
    ../shared/Resources/IBMPlexMono-Regular.ttf
    ../shared/Resources/IBMPlexMono-Medium.ttf
    ../shared/Resources/IBMPlexMono-SemiBold.ttf)

set(BSPACE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ../shared/smp/Widgets.cpp
    ../shared/smp/Shell.cpp)

target_sources(BSpace PRIVATE ${BSPACE_SOURCES})
target_include_directories(BSpace PRIVATE Source ../shared)
target_compile_definitions(BSpace PRIVATE SMP_ACCENT_ARGB=BSUITE_ACCENT_SPACE)   
target_compile_definitions(BSpace PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(BSpace
    PRIVATE BSpaceAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- Test (riga di comando)
juce_add_console_app(BSpaceTests PRODUCT_NAME "BSpaceTests")
target_sources(BSpaceTests PRIVATE Tests/Tests.cpp ${BSPACE_SOURCES})
target_include_directories(BSpaceTests PRIVATE Source ../shared)
target_compile_definitions(BSpaceTests PRIVATE SMP_ACCENT_ARGB=BSUITE_ACCENT_SPACE JUCE_MODAL_LOOPS_PERMITTED=1)
target_compile_definitions(BSpaceTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(BSpaceTests PRIVATE BSpaceAssets juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
