# The cmake-consumer example: teta-wot-cpp used from another CMake project.
#
# Built two ways:
# - in the tree, as every example (`teta_wot_add_example`);
# - on its own, against an installed teta-wot-cpp:
#     cmake -S examples/cmake-consumer -B build-consumer -DCMAKE_PREFIX_PATH=<install prefix>
#   (with vcpkg, the overlay port from Phase 10 replaces the prefix).
if(NOT TARGET teta::wot)
    cmake_minimum_required(VERSION 3.28)
    project(cmake-consumer LANGUAGES CXX)
    find_package(teta-wot CONFIG REQUIRED)
    add_executable(example-cmake-consumer main.cpp)
    target_link_libraries(example-cmake-consumer PRIVATE teta::wot)
    enable_testing()
    add_test(NAME example.cmake-consumer COMMAND example-cmake-consumer)
    return()
endif()

teta_wot_add_example(cmake-consumer SOURCES main.cpp)
