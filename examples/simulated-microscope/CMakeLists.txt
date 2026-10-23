# The microscope's Things, as a library: the example's program links it, and
# so does microscope-service. `microscope.json` is compiled in, as
# `microscope::config`.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/microscope.json microscope_json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS microscope.json)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/microscope_config.hpp
    CONTENT "// Generated from microscope.json by CMake.\n#pragma once\n#include <string_view>\nnamespace microscope {\ninline constexpr std::string_view microscope_json = R\"json(@microscope_json@)json\";\n}\n"
    @ONLY)
add_library(simulated-microscope-lib STATIC microscope.cpp)
target_include_directories(simulated-microscope-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(simulated-microscope-lib PUBLIC teta::wot PRIVATE teta_wot_warnings)
# The image arithmetic gives teta-wot's numbers bit for bit only without
# fused multiply-adds, which GCC and Clang make where the target has them
# (MSVC's /fp:precise doesn't).
if(NOT MSVC)
    target_compile_options(simulated-microscope-lib PRIVATE -ffp-contract=off)
endif()
set_target_properties(simulated-microscope-lib PROPERTIES FOLDER examples)

teta_wot_add_example(simulated-microscope SOURCES main.cpp)
target_link_libraries(example-simulated-microscope PRIVATE simulated-microscope-lib)

# The scripted session, with LabThings' Python client: skipped without
# labthings-fastapi (and httpx, which it brings).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME example.simulated-microscope.demo-py
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/demo.py
            --spawn $<TARGET_FILE:example-simulated-microscope> --out ${CMAKE_CURRENT_BINARY_DIR}/capture.jpg)
    set_tests_properties(example.simulated-microscope.demo-py PROPERTIES LABELS example TIMEOUT 120
        SKIP_REGULAR_EXPRESSION "No module named '(labthings_fastapi|httpx)'")
endif()
