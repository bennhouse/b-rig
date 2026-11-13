cmake_minimum_required(VERSION 3.22)

# versione (file VERSION.txt della radice), architetture macOS, opzioni del compilatore e JUCE: comuni ad app e plugin
include("${CMAKE_CURRENT_LIST_DIR}/../shared/cmake/Suite.cmake")
project(BRig VERSION ${BSUITE_VERSION} LANGUAGES C CXX)
bsuite_setup()

juce_add_gui_app(BRig
    COMPANY_NAME "b-suite"
    BUNDLE_ID com.bsuite.brig
    PRODUCT_NAME "b-rig"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "b-rig needs the audio input to play your guitar through the b-suite chain.")

juce_add_binary_data(BRigAssets SOURCES
    ../shared/Resources/IBMPlexSans-Light.ttf
    ../shared/Resources/IBMPlexSans-Regular.ttf
    ../shared/Resources/IBMPlexSans-Medium.ttf
    ../shared/Resources/IBMPlexSans-SemiBold.ttf
    ../shared/Resources/IBMPlexSans-Bold.ttf
    ../shared/Resources/IBMPlexMono-Regular.ttf
    ../shared/Resources/IBMPlexMono-Medium.ttf
    ../shared/Resources/IBMPlexMono-SemiBold.ttf)

# I moduli: ogni plugin della suite compilato dai SUOI sorgenti (../b-xxx/Source), nel proprio namespace.
file(GLOB BRIG_MODULES CONFIGURE_DEPENDS Source/Modules/Mod_*.cpp)
set(BRIG_SOURCES
    ${BRIG_MODULES}
    Source/Modules/Modules.cpp
    Source/Engine/RigEngine.cpp
    Source/UI/RigComponent.cpp
    ../shared/smp/Widgets.cpp)

set(BRIG_DEFS SMP_ACCENT_ARGB=BSUITE_ACCENT_RIG JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="b-rig" JUCE_APPLICATION_VERSION_STRING="${BSUITE_VERSION}")
if(MSVC)
    list(APPEND BRIG_DEFS _USE_MATH_DEFINES NOMINMAX)   # M_PI nei DSP; niente macro min/max di windows.h
endif()
set(BRIG_INCLUDES Source ../shared ..)

target_sources(BRig PRIVATE Source/Main.cpp ${BRIG_SOURCES})
if(APPLE)
    target_sources(BRig PRIVATE Source/MacWindow.mm)
endif()
target_include_directories(BRig PRIVATE ${BRIG_INCLUDES})
target_compile_definitions(BRig PRIVATE ${BRIG_DEFS})
target_link_libraries(BRig
    PRIVATE BRigAssets juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- b-rig come plugin (AU, VST3): la stessa catena dentro una DAW, mono -> stereo, i preset dell'app
juce_add_plugin(BRigPlugin
    COMPANY_NAME "b-suite"
    BUNDLE_ID com.bsuite.brig.plugin
    PLUGIN_MANUFACTURER_CODE Bsui
    PLUGIN_CODE Brig
    FORMATS AU VST3
    PRODUCT_NAME "b-rig"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD ${BSUITE_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)
target_sources(BRigPlugin PRIVATE Source/Plugin/RigPlugin.cpp ${BRIG_SOURCES})
target_include_directories(BRigPlugin PRIVATE ${BRIG_INCLUDES})
target_compile_definitions(BRigPlugin PRIVATE ${BRIG_DEFS})
target_compile_definitions(BRigPlugin PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(BRigPlugin
    PRIVATE BRigAssets juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- Test (riga di comando)
juce_add_console_app(BRigTests PRODUCT_NAME "BRigTests")
target_sources(BRigTests PRIVATE Tests/Tests.cpp Source/Plugin/RigPlugin.cpp ${BRIG_SOURCES})
target_include_directories(BRigTests PRIVATE ${BRIG_INCLUDES})
target_compile_definitions(BRigTests PRIVATE ${BRIG_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(BRigTests PRIVATE BRigAssets juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# I sorgenti dei plugin dentro i moduli definiscono funzioni libere (createPluginFilter...) senza prototipo: normale qui.
if(NOT MSVC)
    set_source_files_properties(${BRIG_MODULES} PROPERTIES COMPILE_OPTIONS "-Wno-missing-prototypes;-Wno-shadow")
endif()


# macOS ricorda il permesso del microfono solo per un'app firmata con l'Info.plist legato: la firma del solo
# linker non basta (chiederebbe il permesso a ogni apertura del dispositivo audio). Firma ad-hoc del bundle.
if(APPLE)
    add_custom_command(TARGET BRig POST_BUILD
        COMMAND codesign --force --deep --sign - --identifier com.bsuite.brig "$<TARGET_BUNDLE_DIR:BRig>"
        VERBATIM)
endif()


# Generatore dell'icona (si lancia a mano quando cambia: ./build/BRigIcon_artefacts/Release/BRigIcon Resources/AppIcon.png)
juce_add_console_app(BRigIcon PRODUCT_NAME "BRigIcon")
target_sources(BRigIcon PRIVATE Tools/MakeIcon.cpp)
target_include_directories(BRigIcon PRIVATE ${BRIG_INCLUDES})
target_compile_definitions(BRigIcon PRIVATE ${BRIG_DEFS})
target_link_libraries(BRigIcon PRIVATE BRigAssets juce::juce_gui_basics juce::juce_recommended_config_flags)

# Logo della b-suite (banner, schermo intero, icona; SVG, testo in tracciati): ./build/BSuiteLogo_artefacts/Release/BSuiteLogo ../docs
juce_add_console_app(BSuiteLogo PRODUCT_NAME "BSuiteLogo")
target_sources(BSuiteLogo PRIVATE Tools/MakeLogo.cpp)
target_include_directories(BSuiteLogo PRIVATE ${BRIG_INCLUDES})
target_compile_definitions(BSuiteLogo PRIVATE ${BRIG_DEFS})
target_link_libraries(BSuiteLogo PRIVATE BRigAssets juce::juce_gui_basics juce::juce_recommended_config_flags)
