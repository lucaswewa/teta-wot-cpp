# property-cells

**Concept:** `Prop<T>`, the typed cells that hold a Thing's data properties: how values are validated, who is notified of changes, and what `read_only` means.

**Technologies:** `tw::Prop<T>` with `td::Constraints` (`ge`, `le`, `min_length`, `max_length`, `pattern`), `Watcher<T>` (in place of `tokio::sync::watch`), the pydantic-compatible validator (lax coercion, 422 error bodies), the `MessageBroker`, and `PropertyEntry` (what a client's request reaches). The C++ port of teta-wot's `property-cells`.

## Run it

```
cmake --build --preset linux-gcc --target example-property-cells
./build/linux-gcc/examples/property-cells/example-property-cells
```

It prints what each step observes, the same lines as the Rust example, and exits with a non-zero status if a check fails.

## What to look for

- `set(120.0)` from C++ is refused with pydantic's message, and the value isn't stored; a client's `"55"` is coerced to `55.0`.
- A client's `null` is a missing body (`missing`), even for a property whose type allows `null`.
- Read-only refuses clients only: the Thing's own `set` works.
