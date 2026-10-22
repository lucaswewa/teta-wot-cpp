find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(teta-wot-tests
    core/broker_test.cpp
    core/cancel_test.cpp
    core/device_test.cpp
    core/log_test.cpp
    core/nested_test.cpp
    core/property_test.cpp
    core/runtime_test.cpp
    core/thing_spec_test.cpp
    http/binding_test.cpp
    http/cors_test.cpp
    http/fixtures_test.cpp
    http/http_test.cpp
    http/options_test.cpp
    json/json_test.cpp
    json/parse_test.cpp
    td/codec_corpus_test.cpp
    td/model_test.cpp
    td/schema_corpus_test.cpp
    td/twin_tds_test.cpp
    td/validation_test.cpp
    validate/http_validation_test.cpp
    validate/validate_test.cpp
    validate/validation_corpus_test.cpp)
target_link_libraries(teta-wot-tests PRIVATE teta_wot_twins teta::wot teta::wot-td-validation GTest::gtest_main teta_wot_warnings)
# The HTTP binding's own headers, for its unit tests.
target_include_directories(teta-wot-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(teta-wot-tests PRIVATE
    TETA_WOT_SERDE_JSON_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/json/serde_json_corpus.txt"
    TETA_WOT_SCHEMA_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/td/schema_corpus.jsonl"
    TETA_WOT_FIXTURES="${PROJECT_SOURCE_DIR}/conformance/fixtures"
    TETA_WOT_SCENARIOS="${PROJECT_SOURCE_DIR}/conformance/scenarios"
    TETA_WOT_VALIDATION_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/validate/validation_corpus.jsonl"
    TETA_WOT_WIRE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/http/wire_corpus.jsonl")
gtest_discover_tests(teta-wot-tests DISCOVERY_MODE PRE_TEST)

add_subdirectory(compile_fail)

# The HTTP fixtures over real sockets: run.py against the C++ twin server.
# `tds` and `tds-wot` need Things of later phases.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME conformance.http
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/conformance/run.py check
            --server $<TARGET_FILE:twin-server> counter validation default locked prefixed wot)
    set_tests_properties(conformance.http PROPERTIES LABELS conformance TIMEOUT 600)
    # The examples that serve, against their Rust originals' answers (plan
    # section 6.1). They listen on port 5000.
    if(TETA_WOT_BUILD_EXAMPLES)
        add_test(NAME conformance.examples
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/conformance/run.py check --examples
                --server ${PROJECT_BINARY_DIR}/examples/{name}/example-{name}${CMAKE_EXECUTABLE_SUFFIX})
        set_tests_properties(conformance.examples PROPERTIES LABELS conformance TIMEOUT 600 RESOURCE_LOCK port-5000)
    endif()
else()
    message(STATUS "Python 3 not found: the conformance.http test is skipped")
endif()
