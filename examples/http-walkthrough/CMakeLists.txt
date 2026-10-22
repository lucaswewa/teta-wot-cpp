teta_wot_add_example(http-walkthrough SOURCES main.cpp)
if(WIN32)
    target_link_libraries(example-http-walkthrough PRIVATE ws2_32)
endif()
