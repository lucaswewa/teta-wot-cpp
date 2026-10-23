# The soak tool (a port of teta-wot's `tools/soak`), and the
# shutdown-under-load test, which is its `--stop-under-load` mode on a short
# run against the simulated microscope.
add_executable(soak main.cpp)
target_link_libraries(soak PRIVATE teta::wot teta_wot_warnings $<$<PLATFORM_ID:Windows>:ws2_32 psapi>)
set_target_properties(soak PROPERTIES FOLDER tools)
add_dependencies(soak example-simulated-microscope)

add_test(NAME tools.shutdown-under-load
    COMMAND soak --server $<TARGET_FILE:example-simulated-microscope> --duration 30s --stop-under-load)
set_tests_properties(tools.shutdown-under-load PROPERTIES LABELS tools TIMEOUT 300)
