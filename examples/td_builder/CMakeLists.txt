# Needs teta::wot-td-validation (TETA_WOT_TD_VALIDATION).
if(TARGET teta::wot-td-validation)
    teta_wot_add_example(td_builder SOURCES main.cpp)
    target_link_libraries(example-td_builder PRIVATE teta::wot-td-validation)
endif()
