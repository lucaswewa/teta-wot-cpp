# units-and-semantics

**Concept:** annotating a Thing Description with units and semantic types.

**Technologies:** `t.semantic_type(...)` and `t.context(prefix, iri)` on the Thing; `.unit(...)` and `.semantic_type(...)` on properties and actions; TD validation against the W3C TD 1.1 JSON Schema (`teta::wot-td-validation`, when it is built). The C++ port of teta-wot's `units-and-semantics`.

## Run it

```
cmake --build --preset linux-gcc --target example-units-and-semantics
./build/linux-gcc/examples/units-and-semantics/example-units-and-semantics
```

It prints the TD's `@context`, `@type`s and units, validates the TD, and exits with a non-zero status if the TD is invalid.

## What to look for

- The prefixes (`saref`, `om`) join TD 1.1's context, so compact terms like `saref:Sensor` resolve.
- `unit` and `@type` sit on the affordances; the action's `@type` too.
