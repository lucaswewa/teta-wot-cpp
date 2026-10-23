# Runs the openapi-export example (EXAMPLE) into OUTPUT, and compares the
# OpenAPI document it writes with teta-wot's example's (EXPECTED), byte for
# byte. Run with `cmake -P`.
execute_process(COMMAND "${EXAMPLE}" "${OUTPUT}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${EXAMPLE} failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUTPUT}/openapi.json" "${EXPECTED}"
    RESULT_VARIABLE differ)
if(NOT differ EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}/openapi.json differs from ${EXPECTED}")
endif()
