teta_wot_add_example(docs-offline SOURCES main.cpp)
# Without TETA_WOT_DOCS_OFFLINE, the demo has nothing to check: it exits 77.
set_tests_properties(example.docs-offline PROPERTIES SKIP_RETURN_CODE 77)
