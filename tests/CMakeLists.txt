find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(teta-wot-tests
    core/blob_test.cpp
    core/broker_test.cpp
    core/cancel_test.cpp
    core/device_test.cpp
    core/log_test.cpp
    core/nested_test.cpp
    core/property_test.cpp
    core/runtime_test.cpp
    core/settings_test.cpp
    core/slots_test.cpp
    core/stream_test.cpp
    core/thing_spec_test.cpp
    http/binding_test.cpp
    http/cors_test.cpp
    http/discovery_test.cpp
    http/fallback_test.cpp
    http/fixtures_test.cpp
    http/http_test.cpp
    http/openapi_test.cpp
    http/options_test.cpp
    http/output_test.cpp
    http/websocket_test.cpp
    json/json_test.cpp
    json/parse_test.cpp
    server/cli_test.cpp
    server/config_test.cpp
    server/harness_test.cpp
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
    set(http_scenarios counter validation default locked prefixed wot settings settings-load ws sse sse-wot events
        blobs blobs-wot streams openapi openapi-wot openapi-prefixed discovery discovery-wot security-basic
        security-bearer)
    if(NOT TETA_WOT_DOCS_OFFLINE)
        # The reference serves the docs pages without their assets; with the
        # option, the pages load them from the server (the docs-offline
        # example's scenario).
        list(APPEND http_scenarios docs)
    endif()
    add_test(NAME conformance.http
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/conformance/run.py check
            --server $<TARGET_FILE:twin-server> ${http_scenarios})
    set_tests_properties(conformance.http PROPERTIES LABELS conformance TIMEOUT 600)
    # The command line: exit codes, output, the fallback server.
    add_test(NAME conformance.cli
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/conformance/run.py check --cli
            --server $<TARGET_FILE:twin-server>)
    set_tests_properties(conformance.cli PROPERTIES LABELS conformance TIMEOUT 600)
    # The examples that serve, against their Rust originals' answers (plan
    # section 6.1). They listen on port 5000.
    if(TETA_WOT_BUILD_EXAMPLES)
        set(examples_pattern ${PROJECT_BINARY_DIR}/examples/{name}/example-{name}${CMAKE_EXECUTABLE_SUFFIX})
        file(GLOB example_scenarios CONFIGURE_DEPENDS RELATIVE ${PROJECT_SOURCE_DIR}/conformance/scenarios/examples
            ${PROJECT_SOURCE_DIR}/conformance/scenarios/examples/*.json)
        list(TRANSFORM example_scenarios REPLACE "[.]json$" "")
        if(NOT TETA_WOT_DOCS_OFFLINE)
            # Its pages load their assets from the server only with the option.
            list(REMOVE_ITEM example_scenarios docs-offline)
        endif()
        add_test(NAME conformance.examples
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/conformance/run.py check --examples
                --server ${examples_pattern} ${example_scenarios})
        set_tests_properties(conformance.examples PROPERTIES LABELS conformance TIMEOUT 600 RESOURCE_LOCK port-5000)
    endif()
    # Every OpenAPI document served is valid (skipped without
    # openapi-spec-validator).
    add_test(NAME conformance.openapi
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/conformance/validate_openapi.py
            --server $<TARGET_FILE:twin-server>
            $<$<BOOL:${TETA_WOT_BUILD_EXAMPLES}>:--examples=${PROJECT_BINARY_DIR}/examples/{name}/example-{name}${CMAKE_EXECUTABLE_SUFFIX}>)
    set_tests_properties(conformance.openapi PROPERTIES LABELS conformance TIMEOUT 600 RESOURCE_LOCK port-5000
        SKIP_RETURN_CODE 77)
    # The twin server's DNS-SD advertisement, found by Python's zeroconf
    # (skipped without it).
    if(TETA_WOT_MDNS)
        add_test(NAME conformance.mdns
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/conformance/browse_mdns.py
                --server $<TARGET_FILE:twin-server>)
        set_tests_properties(conformance.mdns PROPERTIES LABELS conformance TIMEOUT 120 SKIP_RETURN_CODE 77)
    endif()
else()
    message(STATUS "Python 3 not found: the conformance.http test is skipped")
endif()
# openapi-export writes the OpenAPI document byte for byte as teta-wot's
# example writes it.
if(TETA_WOT_BUILD_EXAMPLES)
    add_test(NAME conformance.openapi-export
        COMMAND ${CMAKE_COMMAND} -DEXAMPLE=$<TARGET_FILE:example-openapi-export>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/openapi-export
            -DEXPECTED=${PROJECT_SOURCE_DIR}/conformance/fixtures/examples/openapi-export/openapi.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/compare_export.cmake)
    set_tests_properties(conformance.openapi-export PROPERTIES LABELS conformance)
endif()
