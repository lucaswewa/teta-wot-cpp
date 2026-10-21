find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(teta-wot-tests json/json_test.cpp)
target_link_libraries(teta-wot-tests PRIVATE teta::wot-json GTest::gtest_main teta_wot_warnings)
target_compile_definitions(teta-wot-tests PRIVATE
    TETA_WOT_SERDE_JSON_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/json/serde_json_corpus.txt")
gtest_discover_tests(teta-wot-tests DISCOVERY_MODE PRE_TEST)
