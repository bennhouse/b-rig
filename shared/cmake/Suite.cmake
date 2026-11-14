# Comune a b-rig e a tutti i plugin. Si include PRIMA di project() (architetture macOS), poi si chiama
# bsuite_setup() subito dopo project(): C++20, opzioni del compilatore e JUCE aggiunto una volta sola, così app e
# plugin si compilano sia ognuno dalla sua cartella sia tutti insieme dal CMakeLists.txt della radice.
get_filename_component(BSUITE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)   # radice del repository
# una sola versione per l'app e per i plugin che il suo installer può aggiungere: il file VERSION.txt della radice
file(STRINGS "${BSUITE_ROOT}/VERSION.txt" BSUITE_VERSION_FILE LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BSUITE_ROOT}/VERSION.txt")   # riconfigura se cambia
if(DEFINED BSUITE_VERSION_SEEN AND NOT BSUITE_VERSION_SEEN STREQUAL BSUITE_VERSION_FILE)
    # VERSION.txt è cambiato dall'ultima configurazione: una cartella di build già esistente segue la nuova versione
    set(BSUITE_VERSION "${BSUITE_VERSION_FILE}" CACHE STRING "b-rig version (app and plugins)" FORCE)
else()   # prima configurazione: vale -DBSUITE_VERSION=... se c'è, altrimenti VERSION.txt
    set(BSUITE_VERSION "${BSUITE_VERSION_FILE}" CACHE STRING "b-rig version (app and plugins)")
endif()
set(BSUITE_VERSION_SEEN "${BSUITE_VERSION_FILE}" CACHE INTERNAL "")
option(BSUITE_COPY_AFTER_BUILD "Copy the plugins to the system plug-in folders after building" ON)
option(BSUITE_LTO "Link-time optimisation (slower builds; the Windows CI turns it off)" ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)  # universale
endif()

macro(bsuite_setup)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(APPLE)   # in Release il linker toglie il codice che nessuno chiama e i simboli locali: binari più piccoli
        add_link_options("$<$<CONFIG:Release>:LINKER:-dead_strip,-x>")
    endif()
    if(MSVC)
        add_compile_options(/utf-8 /bigobj)                    # sorgenti UTF-8; file con molti simboli
        if(CMAKE_GENERATOR MATCHES "Visual Studio")
            # più file insieme in ogni progetto, ma con un tetto: MSBuild con --parallel compila anche più progetti
            # insieme, e /MP senza numero (un processo per core in ogni progetto) arrivava a core x core compilatori
            # e a decine di GB di RAM. Con --parallel 4 si resta intorno a un compilatore per core.
            # JUCE aggiunge anche un suo /MP senza numero (juce_recommended_config_flags): nel .vcxproj resta il
            # numero di questo. Ninja compila un file per processo e rispetta già -j: lì /MP non fa nulla.
            cmake_host_system_information(RESULT _bsuite_cores QUERY NUMBER_OF_LOGICAL_CORES)
            math(EXPR _bsuite_mp "${_bsuite_cores} / 4")
            if(_bsuite_mp LESS 2)
                set(_bsuite_mp 2)
            endif()
            add_compile_options(/MP${_bsuite_mp})
        endif()
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
        if(NOT BSUITE_LTO)   # senza LTO: i flag del target di JUCE si svuotano per tutti i plugin
            set_target_properties(juce_recommended_lto_flags PROPERTIES INTERFACE_COMPILE_OPTIONS "" INTERFACE_LINK_LIBRARIES "")
        endif()
    endif()
endmacro()
