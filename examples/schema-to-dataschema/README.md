# schema-to-dataschema

**Concept:** how C++ types become TD `DataSchema`s.

**Technologies:** `teta::wot::Codec<T>` (ADR-0007): each type's JSON Schema, shaped as schemars shapes the corresponding Rust type's; Boost.PFR for the fields of aggregate structs; `StructInfo` and `EnumInfo` (through `describe` and `Describe<T>`) for what Rust takes from doc comments and serde attributes; `json_schema_for<T>()` and `for_type<T>()`, which converts the JSON Schema to a TD 1.1 DataSchema; `td::Constraints`. The C++ port of teta-wot's `schema-to-dataschema` example.

## Run it

```
cmake --build --preset linux-gcc --target example-schema-to-dataschema
./build/linux-gcc/examples/schema-to-dataschema/example-schema-to-dataschema
```

For each type it prints the JSON Schema its codec generates, then the DataSchema it converts to. It exits with a non-zero status if a conversion that should work fails, or if the recursive type doesn't fail.

## What to look for

- The DataSchemas are teta-wot's, byte for byte: `tests/td/schema_corpus.jsonl` holds what teta-wot makes of the same types in Rust, and the codec tests compare.
- The JSON Schemas are inlined: no `$defs`, except for a struct that contains itself.
- C++ has no doc comments or serde attributes to read: a struct's static `describe` says which fields default and what they mean, and a specialisation of `Describe<E>` names an enum's values.
