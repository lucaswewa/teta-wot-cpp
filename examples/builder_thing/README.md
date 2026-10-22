# builder_thing

**Concept:** A Thing defined with the definition API, and driven the way the HTTP binding drives it: through the registry, with JSON.

**Technologies:** `ThingSpec` (data and functional properties, actions with typed input and output, start and stop hooks), `Runtime`, `ThingHandle`, `PropertyEntry`, `ActionEntry`, invocations, and the TD from the same definition, validated against the W3C schema. The C++ port of teta-wot's `builder_thing`.

## Run it

```
cmake --build --preset linux-gcc --target example-builder_thing
./build/linux-gcc/examples/builder_thing/example-builder_thing
```

It prints the `select` invocation's record and the TD, as the Rust example does: the TD is the same, byte for byte, and the record but for its ID and times.

## What to look for

- `settle_ms` accepts `"20"` (lax coercion) and refuses 5000 (`le(1000)`).
- `select` with position 9 ends `error` with the handled message.
