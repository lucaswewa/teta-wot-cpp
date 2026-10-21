# td-builder

**Concept:** a W3C Thing Description (TD) built by hand.

**Technologies:** `teta::wot::td` (the `teta::wot-td` library): `ThingBuilder`, the affordance builders, `for_type<T>()`, `Constraints`, and validation against the vendored W3C TD 1.1 JSON Schema (`teta::wot-td-validation`, built with `TETA_WOT_TD_VALIDATION`). The C++ port of teta-wot's `td_builder` example.

## Run it

```
cmake --build --preset linux-gcc --target example-td_builder
./build/linux-gcc/examples/td_builder/example-td_builder
```

It prints the TD of an LED illuminator on stdout, then reports on stderr that the TD validates against the W3C schema and that a deliberately broken TD is refused. It exits with a non-zero status if either check fails, which is what the CI test relies on.

## What to look for

- The TD is the same as the Rust example prints, key for key: the builders write teta-wot's order, and `for_type<std::uint8_t>()` gives schemars' `minimum` and `maximum` for a `u8`, narrowed to 100 by the constraint.
- `ThingBuilder::build` checks the rules the W3C schema can't express (here, an operation in the wrong kind of form) and throws `TdError`.
