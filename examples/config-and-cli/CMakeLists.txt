teta_wot_add_example(config-and-cli SOURCES main.cpp)
if(WIN32)
    target_link_libraries(example-config-and-cli PRIVATE ws2_32)
endif()
