# teta::wot-json: JSON values and serde_json-exact serialisation (ADR-0006).
add_library(teta_wot_json json/json.cpp)
add_library(teta::wot-json ALIAS teta_wot_json)
set_target_properties(teta_wot_json PROPERTIES EXPORT_NAME wot-json)
target_include_directories(teta_wot_json PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(teta_wot_json
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE $<BUILD_INTERFACE:teta_wot_warnings>)
target_compile_features(teta_wot_json PUBLIC cxx_std_20)

# teta::wot: what applications link to; it grows with each phase.
add_library(teta_wot INTERFACE)
add_library(teta::wot ALIAS teta_wot)
set_target_properties(teta_wot PROPERTIES EXPORT_NAME wot)
target_link_libraries(teta_wot INTERFACE teta_wot_json teta_wot_td)

# teta::wot-td: the TD model, DataSchema conversion, codecs (Phase 1).
add_library(teta_wot_td
    codec/codec.cpp
    td/constraints.cpp
    td/convert.cpp
    td/data_schema.cpp
    td/describe.cpp
    td/form.cpp
    td/parse.cpp
    td/thing.cpp)
add_library(teta::wot-td ALIAS teta_wot_td)
set_target_properties(teta_wot_td PROPERTIES EXPORT_NAME wot-td)
target_link_libraries(teta_wot_td
    PUBLIC teta_wot_json Boost::pfr
    PRIVATE $<BUILD_INTERFACE:teta_wot_warnings>)

# teta::wot-td-validation: TDs against the W3C TD 1.1 JSON Schema (optional;
# teta-wot's `validation` feature).
if(TETA_WOT_TD_VALIDATION)
    add_library(teta_wot_td_validation td/validation.cpp)
    add_library(teta::wot-td-validation ALIAS teta_wot_td_validation)
    set_target_properties(teta_wot_td_validation PROPERTIES EXPORT_NAME wot-td-validation)
    teta_wot_embed(teta_wot_td_validation "${PROJECT_SOURCE_DIR}/schema/td-json-schema-validation.json" td_json_schema)
    target_link_libraries(teta_wot_td_validation
        PUBLIC teta_wot_td
        PRIVATE nlohmann_json_schema_validator::validator $<BUILD_INTERFACE:teta_wot_warnings>)
endif()
