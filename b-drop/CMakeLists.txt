cmake_minimum_required(VERSION 3.22)

# versione della suite, architetture macOS, opzioni del compilatore e JUCE: comuni a tutti i plugin
include("${CMAKE_CURRENT_LIST_DIR}/../shared/cmake/Suite.cmake")
project(BDrop VERSION ${BSUITE_VERSION} LANGUAGES C CXX)
bsuite_setup()

juce_add_plugin(BDrop
    COMPANY_NAME "b-suite"
    BUNDLE_ID com.bsuite.bdrop
    PLUGIN_MANUFACTURER_CODE Bsui
    PLUGIN_CODE Bdrp
    FORMATS AU VST3
    PRODUCT_NAME "b-drop"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Pitch
    COPY_PLUGIN_AFTER_BUILD ${BSUITE_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

# Font IBM Plex Sans + Mono condivisi (SIL Open Font License, vedi ../shared/Resources/OFL-IBMPlex.txt)
juce_add_binary_data(BDropAssets SOURCES
    ../shared/Resources/IBMPlexSans-Light.ttf
    ../shared/Resources/IBMPlexSans-Regular.ttf
    ../shared/Resources/IBMPlexSans-Medium.ttf
    ../shared/Resources/IBMPlexSans-SemiBold.ttf
    ../shared/Resources/IBMPlexSans-Bold.ttf
    ../shared/Resources/IBMPlexMono-Regular.ttf
    ../shared/Resources/IBMPlexMono-Medium.ttf
    ../shared/Resources/IBMPlexMono-SemiBold.ttf)

set(BDROP_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ../shared/smp/Widgets.cpp
    ../shared/smp/Shell.cpp)

target_sources(BDrop PRIVATE ${BDROP_SOURCES})
target_include_directories(BDrop PRIVATE Source ../shared)
target_compile_definitions(BDrop PRIVATE SMP_ACCENT_ARGB=0xffc084fc)   
target_compile_definitions(BDrop PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(BDrop
    PRIVATE BDropAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- Test (riga di comando)
juce_add_console_app(BDropTests PRODUCT_NAME "BDropTests")
target_sources(BDropTests PRIVATE Tests/Tests.cpp ${BDROP_SOURCES})
target_include_directories(BDropTests PRIVATE Source ../shared)
target_compile_definitions(BDropTests PRIVATE SMP_ACCENT_ARGB=0xffc084fc JUCE_MODAL_LOOPS_PERMITTED=1)
target_compile_definitions(BDropTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(BDropTests PRIVATE BDropAssets juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
