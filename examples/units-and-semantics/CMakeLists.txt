teta_wot_add_example(units-and-semantics SOURCES main.cpp)
# The W3C TD 1.1 JSON Schema check, when the library that has it is built.
if(TARGET teta::wot-td-validation)
    target_link_libraries(example-units-and-semantics PRIVATE teta::wot-td-validation)
    target_compile_definitions(example-units-and-semantics PRIVATE TETA_WOT_TD_VALIDATION)
endif()
