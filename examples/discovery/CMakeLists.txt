teta_wot_add_example(discovery SOURCES main.cpp)
# The client browses with the library the server advertises with.
target_include_directories(example-discovery PRIVATE ${PROJECT_SOURCE_DIR}/third_party/mdns)
target_link_libraries(example-discovery PRIVATE $<$<PLATFORM_ID:Windows>:ws2_32 iphlpapi>)
# Without TETA_WOT_MDNS, the demo has nothing to find: it exits 77.
set_tests_properties(example.discovery PROPERTIES SKIP_RETURN_CODE 77)
