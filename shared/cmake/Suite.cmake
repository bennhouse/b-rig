# Comune a tutti i plugin della b-suite. Si include PRIMA di project() (architetture macOS), poi si chiama
# bsuite_setup() subito dopo project(): C++20, opzioni del compilatore e JUCE aggiunto una volta sola, così i plugin
# si compilano sia ognuno dalla sua cartella sia tutti insieme dal CMakeLists.txt della radice.
get_filename_component(BSUITE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)   # radice del repository
set(BSUITE_VERSION "0.1.0" CACHE STRING "b-suite version (every plugin has the same version)")
option(BSUITE_COPY_AFTER_BUILD "Copy the plugins to the system plug-in folders after building" ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)  # universale
endif()

macro(bsuite_setup)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        add_compile_options(/utf-8 /bigobj)                    # sorgenti UTF-8; file con molti simboli
        add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)    # M_PI nei DSP; niente macro min/max di windows.h
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # runtime statico
    endif()
    # JUCE: il submodule in libs/JUCE (una volta sola anche quando i plugin si compilano insieme), altrimenti lo scarica
    if(NOT TARGET juce::juce_core)
        set(SMP_JUCE_DIR "${BSUITE_ROOT}/libs/JUCE")
        if(EXISTS "${SMP_JUCE_DIR}/CMakeLists.txt")
            add_subdirectory("${SMP_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
        else()
            include(FetchContent)
            FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.5 GIT_SHALLOW TRUE)
            FetchContent_MakeAvailable(JUCE)
        endif()
    endif()
endmacro()
