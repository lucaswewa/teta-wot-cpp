find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(teta-wot-tests
    json/json_test.cpp
    td/codec_corpus_test.cpp
    td/model_test.cpp
    td/schema_corpus_test.cpp
    td/twin_tds_test.cpp
    td/validation_test.cpp
    validate/http_validation_test.cpp
    validate/validate_test.cpp
    validate/validation_corpus_test.cpp)
target_link_libraries(teta-wot-tests PRIVATE teta::wot-json teta::wot-td teta::wot-td-validation teta::wot-core GTest::gtest_main teta_wot_warnings)
target_compile_definitions(teta-wot-tests PRIVATE
    TETA_WOT_SERDE_JSON_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/json/serde_json_corpus.txt"
    TETA_WOT_SCHEMA_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/td/schema_corpus.jsonl"
    TETA_WOT_FIXTURES="${PROJECT_SOURCE_DIR}/conformance/fixtures"
    TETA_WOT_VALIDATION_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/validate/validation_corpus.jsonl")
gtest_discover_tests(teta-wot-tests DISCOVERY_MODE PRE_TEST)
