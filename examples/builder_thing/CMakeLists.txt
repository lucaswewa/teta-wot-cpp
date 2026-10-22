# Validates its TD: needs teta::wot-td-validation (TETA_WOT_TD_VALIDATION).
if(TARGET teta::wot-td-validation)
    teta_wot_add_example(builder_thing SOURCES main.cpp)
    target_link_libraries(example-builder_thing PRIVATE teta::wot-td-validation)
endif()
