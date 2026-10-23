# Arrays

Images, spectra and other arrays of numbers are `tw::NdArray<T, Rank>`: a shape and its elements in row-major order, carried on the wire as nested lists, one level per dimension, as LabThings carries numpy arrays.

```cpp
class Camera {
  public:
    tw::Prop<tw::NdArray<double, 2>> dark_frame{tw::NdArray<double, 2>::zeros({480, 640})};

    tw::NdArray<double, 2> correct(tw::NdArray<double, 2> image);      // a matrix in, a matrix out
    tw::NdArray<std::uint64_t, 1> histogram(tw::NdArray<> image, std::uint64_t bins);  // any rank in
};
```

- A fixed rank (`NdArray<double, 2>`) is described exactly: an array of arrays of numbers.
- `NdArray<>` (`dynamic_rank`) takes any rank, and is described as LabThings describes an `NDArray`: a number, or lists of numbers up to six deep, or seven levels of lists of anything. A zero-dimensional array is its one element (`5`).
- Lists must be rectangular: `[[1, 2], [3]]` is refused with numpy's message, "setting an array element with a sequence. The requested array has an inhomogeneous shape after 1 dimensions…", as a validation error at the array's place in the input.

`from_shape(shape, elements)`, `zeros(shape)` and `filled(shape, value)` make arrays; `shape()`, `ndim()`, `size()`, `elements()` (a span), `at({i, j})`, `map(f)` and `sum()` read them. The `ndarray-property` example shows them as a property and as actions' inputs and outputs.
