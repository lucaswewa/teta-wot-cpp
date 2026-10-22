# Things declared wrongly must not compile, with a message that says why
# (plan §2.3; the counterpart of teta-wot's trybuild tests). Each case is a
# target left out of the build, and a test that builds it and expects the
# compiler's output to match.
set(cases
    "no_codec|PropertyValue"
    "not_copyable|PropertyValue"
    "parameter_names|name every parameter of the action"
    "parameter_codec|an action parameter needs a codec that reads it"
    "parameter_reference|take them by value or const&"
    "setter_type|a setter takes a value of the property's type"
    "getter_parameters|a property's getter takes nothing"
    "not_a_thing|ThingType"
    "output_codec|an action's output needs a codec")
foreach(entry IN LISTS cases)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 expected)
    add_executable(compile_fail_${name} EXCLUDE_FROM_ALL ${name}.cpp)
    target_link_libraries(compile_fail_${name} PRIVATE teta::wot)
    add_test(NAME compile_fail.${name}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target compile_fail_${name} --config $<CONFIG>)
    set_tests_properties(compile_fail.${name} PROPERTIES
        PASS_REGULAR_EXPRESSION "${expected}" LABELS compile_fail TIMEOUT 300)
endforeach()
