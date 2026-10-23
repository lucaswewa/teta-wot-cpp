cmake_minimum_required(VERSION 3.28)

project(teta-wot-cpp
    VERSION 0.0.0
    DESCRIPTION "W3C Web of Things Things over HTTP with Drogon, with wire-level parity with teta-wot"
    HOMEPAGE_URL "https://github.com/lucaswewa/teta-wot-cpp"
    LANGUAGES CXX)

# C++20 (ADR-0002).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}" TETA_WOT_TOP_LEVEL)
option(TETA_WOT_BUILD_TESTS "Build the unit tests" ${TETA_WOT_TOP_LEVEL})
option(TETA_WOT_BUILD_EXAMPLES "Build the examples, and run them as tests" ${TETA_WOT_TOP_LEVEL})
option(TETA_WOT_BUILD_SPIKES "Build the Phase 0 spikes" OFF)
option(TETA_WOT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(TETA_WOT_TD_VALIDATION "Build teta::wot-td-validation (needs json-schema-validator)" ${TETA_WOT_BUILD_TESTS})
option(TETA_WOT_DOCS_OFFLINE "Compile the Swagger UI and ReDoc assets into the server, for networks without internet" OFF)
option(TETA_WOT_MDNS "Let servers advertise themselves with DNS-SD over mDNS (W3C WoT Discovery)" OFF)
option(TETA_WOT_BUILD_FUZZ "Build the fuzz targets, and run them briefly as tests" ${TETA_WOT_BUILD_TESTS})
option(TETA_WOT_LIBFUZZER "Link the fuzz targets with libFuzzer (Clang) instead of their driver" OFF)
option(TETA_WOT_BUILD_TOOLS "Build the soak tool, and run the shutdown-under-load test" ${TETA_WOT_BUILD_EXAMPLES})

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(TetaWotWarnings)
include(TetaWotEmbed)

find_package(nlohmann_json 3.11 CONFIG REQUIRED)
find_package(boost_pfr CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(tl-expected CONFIG REQUIRED)
# The HTTP server (ADR-0004).
find_package(Drogon CONFIG REQUIRED)
if(TETA_WOT_TD_VALIDATION)
    find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
endif()

add_subdirectory(src)

if(TETA_WOT_BUILD_TESTS OR TETA_WOT_BUILD_EXAMPLES OR TETA_WOT_BUILD_FUZZ)
    enable_testing()
endif()
if(TETA_WOT_BUILD_TESTS)
    add_subdirectory(conformance/twins)
    add_subdirectory(conformance/twin-server-cpp)
    add_subdirectory(tests)
endif()
if(TETA_WOT_BUILD_EXAMPLES)
    include(TetaWotExamples)
    add_subdirectory(examples)
endif()
if(TETA_WOT_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()
# The soak tool drives the simulated microscope example.
if(TETA_WOT_BUILD_TOOLS AND TETA_WOT_BUILD_EXAMPLES)
    add_subdirectory(tools/soak)
endif()
if(TETA_WOT_BUILD_SPIKES)
    add_subdirectory(spikes/drogon)
endif()

# Installation, for `find_package(teta-wot)` (the cmake-consumer example).
include(CMakePackageConfigHelpers)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS teta_wot_json teta_wot_td teta_wot_core teta_wot_http teta_wot_drogon teta_wot_server teta_wot
    EXPORT teta-wot-targets)
if(TETA_WOT_TD_VALIDATION)
    install(TARGETS teta_wot_td_validation EXPORT teta-wot-targets)
endif()
install(EXPORT teta-wot-targets
    NAMESPACE teta::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/teta-wot)
configure_package_config_file(cmake/teta-wot-config.cmake.in
    ${PROJECT_BINARY_DIR}/teta-wot-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/teta-wot)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/teta-wot-config-version.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES
    ${PROJECT_BINARY_DIR}/teta-wot-config.cmake
    ${PROJECT_BINARY_DIR}/teta-wot-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/teta-wot)
