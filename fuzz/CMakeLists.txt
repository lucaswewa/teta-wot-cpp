# The fuzz targets (plan section 8): JSON, validation, the WebSocket dialect
# and request bodies. Each defines `fuzz::one_input`. With
# TETA_WOT_LIBFUZZER (Clang), each is a libFuzzer binary:
#
#     ./build/<preset>/fuzz/fuzz-json -dict=fuzz/json.dict -max_total_time=600 corpus/json
#
# with a corpus directory from `make_seeds.py --dir corpus`. Otherwise each is
# linked with driver.cpp, which runs the seeds in fuzz/seeds and a fixed
# number of mutations of them, and CTest runs it (label `fuzz`).
set(fuzz_targets json validation websocket request_body)
# Mutations per CTest run, sized to take seconds in a debug build.
set(fuzz_iterations_json 20000)
set(fuzz_iterations_validation 20000)
set(fuzz_iterations_websocket 5000)
set(fuzz_iterations_request_body 5000)

foreach(target IN LISTS fuzz_targets)
    string(REPLACE "_" "-" name ${target})
    add_executable(fuzz-${name} ${target}.cpp)
    target_link_libraries(fuzz-${name} PRIVATE teta::wot teta_wot_warnings)
    set_target_properties(fuzz-${name} PROPERTIES FOLDER fuzz)
    if(TETA_WOT_LIBFUZZER)
        target_sources(fuzz-${name} PRIVATE entry.cpp)
        target_compile_options(fuzz-${name} PRIVATE -fsanitize=fuzzer)
        target_link_options(fuzz-${name} PRIVATE -fsanitize=fuzzer)
    else()
        target_sources(fuzz-${name} PRIVATE driver.cpp)
        add_test(NAME fuzz.${name}
            COMMAND fuzz-${name} --iterations ${fuzz_iterations_${target}} --dict ${CMAKE_CURRENT_SOURCE_DIR}/json.dict
                ${CMAKE_CURRENT_SOURCE_DIR}/seeds/${target}.seeds
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(fuzz.${name} PROPERTIES LABELS fuzz TIMEOUT 600)
    endif()
endforeach()
