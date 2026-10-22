teta_wot_add_example(invocation-logs SOURCES main.cpp)
# The pump driver logs with spdlog when it is available (the vcpkg feature
# `spdlog`), through teta-wot-cpp's spdlog sink; otherwise with tw::log.
find_package(spdlog CONFIG QUIET)
if(spdlog_FOUND)
    target_link_libraries(example-invocation-logs PRIVATE spdlog::spdlog)
    target_compile_definitions(example-invocation-logs PRIVATE TETA_WOT_EXAMPLE_SPDLOG)
endif()
