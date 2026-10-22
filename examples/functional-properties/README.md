# functional-properties

**Concept:** Functional properties: a getter, and optionally a setter, a resetter and a default.

**Technologies:** `ThingSpec::property` with a member function, `.setter()`, `.resetter()`, `.default_value()`, constraints checked before the setter, `.read_only()`. The C++ port of teta-wot's `functional-properties`, in-process: the requests the Rust example makes over HTTP are calls on the property registry here (HTTP arrives in Phase 4).

## Run it

```
cmake --build --preset linux-gcc --target example-functional-properties
./build/linux-gcc/examples/functional-properties/example-functional-properties
```

It prints each property's TD entry and what each request gets, and exits with a non-zero status if an outcome isn't the expected one.

## What to look for

- The TD lines are the same as the Rust example's: functional properties are read-only without a setter, and aren't observable.
- Every getter runs on the calling worker thread, so a slow synchronous read needs no `blocking` marker (accepted, and ignored, for parity).
- `power_limit` has a setter but is `read_only`: clients are refused, C++ code calls it.
