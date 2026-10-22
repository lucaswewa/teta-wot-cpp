// What the compiler says when a Thing is declared wrongly.
//
// Rust's macros reject a badly declared Thing at compile time; so do C++'s
// concepts and static_asserts. The cases are in tests/compile_fail/, one file
// each, and CI builds each one and checks that it fails with its message.
// This demonstration lists them; the README shows and explains them.

#include <cstdlib>
#include <iostream>

struct Case {
    const char* file;
    const char* mistake;
    const char* message;
};

constexpr Case cases[] = {
    {"no_codec.cpp", "a property of a type without a codec",
     "constraints not satisfied: PropertyValue<Opaque> (Decodable: no Codec<Opaque>)"},
    {"not_copyable.cpp", "a property of a type that can't be copied (std::unique_ptr)",
     "constraints not satisfied: PropertyValue<std::unique_ptr<int>>"},
    {"parameter_names.cpp", "an action with two parameters and one name",
     "name every parameter of the action, in order, except ActionCtx&, Server and Dep<S>"},
    {"parameter_codec.cpp", "an action parameter of a type without a codec",
     "an action parameter needs a codec that reads it (Codec<T> with decode)"},
    {"parameter_reference.cpp", "an action parameter taken by non-const reference",
     "an action's parameters are read from the request: take them by value or const&"},
    {"setter_type.cpp", "a setter of another type than its getter",
     "a setter takes a value of the property's type (the getter's)"},
    {"getter_parameters.cpp", "a property's getter with a parameter", "a property's getter takes nothing"},
    {"not_a_thing.cpp", "a class without `define` added as a Thing",
     "constraints not satisfied: ThingType<Lamp>"},
    {"output_codec.cpp", "an action returning a type without a codec",
     "an action's output needs a codec (Codec<T>)"},
};

int main() {
    std::cout << "Declarations that don't compile (tests/compile_fail/), and what the compiler says:\n\n";
    for (const auto& c : cases) {
        std::cout << c.file << "\n  mistake: " << c.mistake << "\n  error:   " << c.message << "\n";
    }
    return EXIT_SUCCESS;
}
