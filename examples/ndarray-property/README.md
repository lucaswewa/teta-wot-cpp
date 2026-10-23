# ndarray-property

**Concept:** arrays as properties and as actions' inputs and outputs. `tw::NdArray<T, Rank>` holds a shape and its elements, and is carried as nested lists, one level per dimension.

**Technologies:** `tw::NdArray<double, 2>` (a fixed rank), `tw::NdArray<>` (any rank) and `tw::NdArray<std::uint64_t, 1>`; `from_shape`, `zeros`, `elements()`; numpy's message for ragged lists. The C++ port of teta-wot's `ndarray-property`, which uses the `ndarray` crate.

## Run it

```
cmake --build --preset linux-gcc --target example-ndarray-property
./build/linux-gcc/examples/ndarray-property/example-ndarray-property            # in-process, then exit
./build/linux-gcc/examples/ndarray-property/example-ndarray-property --serve    # serve on http://127.0.0.1:5000
```

It prints the TD's description of the arrays and the answers to a few requests, and exits with a non-zero status if one is wrong. Served, for `curl` (on Windows, `curl.exe`):

```
curl http://127.0.0.1:5000/camera/dark_frame
```

## What to look for

- A fixed rank is described exactly in the TD (`dark_frame` is an array of arrays of numbers); any rank is LabThings' union of nested lists (9 branches).
- Integers are read into arrays of doubles, and written back as `1.0`.
- Ragged lists are refused with numpy's message, as LabThings refuses them.
