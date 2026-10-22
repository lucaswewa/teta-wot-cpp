# light

**Concept:** the guide's tutorial Thing: data properties with constraints and a read-only flag, an action, and a functional property computed by a member function.

**Technologies:** `ThingSpec::property` with `.ge()`, `.le()` and `.read_only()`, a functional property from a `const` member function, `ThingSpec::action`, `TestClient`. The C++ port of teta-wot's `light`.

## Run it

```
cmake --build --preset linux-gcc --target example-light
./build/linux-gcc/examples/light/example-light            # a short in-process demo, then exit
./build/linux-gcc/examples/light/example-light --serve    # serve on http://127.0.0.1:5000 until Ctrl-C
```

It prints the same lines as the Rust example, and exits with a non-zero status if an answer is wrong.

## What to look for

- The constraints appear in the TD as `minimum` and `maximum`, and a client's 150 is refused with pydantic's message.
- `is_on` is read-only for clients (`405`), but the Thing's own `toggle` changes it.
- `status` has no setter, so it is read-only too, and is computed on every read.
