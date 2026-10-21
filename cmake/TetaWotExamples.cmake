# teta_wot_add_example(<name> SOURCES <file>... [WINDOWS_ONLY] [NEEDS <tool>...] [ARGS <arg>...])
#
# Builds `examples/<name>` as the executable `example-<name>`, linked to
# teta::wot, and registers its demonstration (the executable run without
# arguments, or with ARGS) as the CTest test `example.<name>`, labelled
# `example`, so CI runs every example on every platform (plan §6.1).
#
# WINDOWS_ONLY examples are skipped elsewhere. NEEDS names programs the
# demonstration runs (python3, node, ...): without them the example is still
# built, but its test is skipped.
function(teta_wot_add_example name)
    cmake_parse_arguments(PARSE_ARGV 1 EXAMPLE "WINDOWS_ONLY" "" "SOURCES;NEEDS;ARGS")
    if(EXAMPLE_WINDOWS_ONLY AND NOT WIN32)
        return()
    endif()
    set(target example-${name})
    add_executable(${target} ${EXAMPLE_SOURCES})
    target_link_libraries(${target} PRIVATE teta::wot teta_wot_warnings)
    set_target_properties(${target} PROPERTIES FOLDER examples)
    add_test(NAME example.${name} COMMAND ${target} ${EXAMPLE_ARGS})
    set_tests_properties(example.${name} PROPERTIES LABELS example TIMEOUT 120)
    foreach(tool IN LISTS EXAMPLE_NEEDS)
        find_program(TETA_WOT_TOOL_${tool} ${tool})
        if(NOT TETA_WOT_TOOL_${tool})
            message(STATUS "example ${name}: ${tool} not found, its test is skipped")
            set_tests_properties(example.${name} PROPERTIES DISABLED TRUE)
        endif()
    endforeach()
endfunction()
