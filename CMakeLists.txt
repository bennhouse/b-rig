# Tutti i plugin della b-suite in una sola build (la usa l'installer della suite; comoda anche in locale):
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target bsuite_plugins
# Ogni plugin si compila anche da solo dalla sua cartella. b-rig ha la sua build in b-rig/.
cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/shared/cmake/Suite.cmake")
project(BSuite VERSION ${BSUITE_VERSION} LANGUAGES C CXX)
bsuite_setup()

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/tools/installer/plugins.txt" BSUITE_LINES REGEX "^b-")
add_custom_target(bsuite_plugins)   # tutti i formati di tutti i plugin
add_custom_target(bsuite_tests)     # i programmi di test
foreach(line IN LISTS BSUITE_LINES)
    string(REPLACE " " ";" parts "${line}")
    list(GET parts 0 dir)
    list(GET parts 1 target)
    add_subdirectory("${dir}")
    add_dependencies(bsuite_plugins ${target}_VST3)
    if(APPLE)
        add_dependencies(bsuite_plugins ${target}_AU)
    endif()
    add_dependencies(bsuite_tests ${target}Tests)
    if(APPLE)   # i test girano sulla macchina che compila: una sola architettura, metà del tempo
        set_target_properties(${target}Tests PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
endforeach()
