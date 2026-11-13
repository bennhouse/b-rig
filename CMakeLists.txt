# b-rig e tutti i plugin in una sola build (la usa l'installer; comoda anche in locale):
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target brig_release
# brig_release = l'app + i plugin che l'installer può aggiungere; bsuite_tests = i programmi di test.
# App e plugin si compilano anche ognuno dalla sua cartella.
cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/shared/cmake/Suite.cmake")
project(BRigProject VERSION ${BSUITE_VERSION} LANGUAGES C CXX)
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

# l'app
add_subdirectory(b-rig)
add_dependencies(bsuite_tests BRigTests)
if(APPLE)
    set_target_properties(BRigTests PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()
add_custom_target(brig_release)
add_dependencies(brig_release BRig bsuite_plugins)
