# compile-errors

**Concept:** What the compiler says when a Thing is declared wrongly.

**Technologies:** Concepts and `static_assert`s in `ThingSpec`, `Prop<T>` and the runtime. The cases are `tests/compile_fail/*.cpp`; CI builds each and checks that it fails with its message (the counterpart of the `trybuild` tests the plan calls for).

## Run it

```
cmake --build --preset linux-gcc --target example-compile-errors
./build/linux-gcc/examples/compile-errors/example-compile-errors
```

Its demonstration lists the cases and their messages.

## What to look for

- A property, parameter or output type needs a `Codec` (`Prop<T>` requires `PropertyValue`, a property value must also be copyable).
- Action parameters are named in order (`t.action<"x", "y">(...)`), and taken by value or `const&`.
- A setter takes the getter's type, a getter takes nothing, and a Thing has `static void define(ThingSpec<T>&)`.
- Names (reserved, duplicated) are checked when the runtime is built, as teta-wot checks them.
