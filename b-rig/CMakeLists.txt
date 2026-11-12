cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)  # universale
endif()

# versione: in CI arriva dal tag della release (b-rig-v0.2.0 -> 0.2.0)
set(BRIG_VERSION "0.1.0" CACHE STRING "b-rig version")
project(BRig VERSION ${BRIG_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8 /bigobj)   # sorgenti UTF-8 (accenti, simboli dell'interfaccia); moduli con molti simboli
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # runtime statico: niente VC++ redistributable
endif()

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

set(BRIG_DEFS SMP_ACCENT_ARGB=0xffe9d8a6 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="b-rig" JUCE_APPLICATION_VERSION_STRING="${BRIG_VERSION}")
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

# ---- Test (riga di comando)
juce_add_console_app(BRigTests PRODUCT_NAME "BRigTests")
target_sources(BRigTests PRIVATE Tests/Tests.cpp ${BRIG_SOURCES})
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
