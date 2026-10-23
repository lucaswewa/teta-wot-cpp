# simulated-microscope

**Concept:** the capstone. A stage, a camera and an autofocus, composed with slots, with settings, an MJPEG stream, Blobs, events and arrays, served from a configuration file and driven by a Python client.

- **`Stage`**
  - moves x, y and z at a `speed` that is a setting;
  - each move takes time, in small steps, and can be cancelled;
  - it emits `arrived`.
- **`Camera`** looks at the slide through the stage (a slot). A thread renders what it sees into an MJPEG preview: a field of cells, sharp at z = 250 and blurred away from it.
  - `capture` returns a JPEG Blob;
  - `sharpness` measures the image;
  - `exposure` is a setting;
  - it emits `captured`.
- **`Autofocus`** sweeps the stage's z (one slot) through the camera's sharpness (another), moves to the sharpest position, returns the focus curve as an `NdArray`, and emits `focused`.

**Technologies:**

- `tw::Slot<T>`, `.setting()`, `t.stream(...)`, `tw::Event<T>`, `tw::Blob<tw::blob::Jpeg>`, `tw::NdArray<double, 2>`, `t.on_start` and `t.on_stop`, a feeding `std::jthread`;
- `tw::ThingRegistry` and `tw::cli::serve_from_cli`;
- the examples' JPEG encoder (`examples/common/jpeg.hpp`), here for greyscale images;
- LabThings' `ThingClient` and `httpx` in `demo.py`.

The C++ port of teta-wot's `simulated-microscope`, with the same `microscope.json` and `demo.py`. The library (`microscope.hpp`, `microscope.cpp`, the CMake target `simulated-microscope-lib`) is shared by the example's program and `microscope-service`. The image arithmetic is teta-wot's, operation for operation, so the sharpnesses and the focus curve are the same numbers; the JPEGs are this project's encoder's, not `jpeg-encoder`'s, so their sizes differ.

## Run it

```
cmake --build --preset linux-gcc --target example-simulated-microscope
./build/linux-gcc/examples/simulated-microscope/example-simulated-microscope
```

Without arguments, it runs the demonstration in-process and exits with a non-zero status if a step fails:

1. it moves the stage and changes a setting;
2. it captures and downloads an image;
3. it runs the autofocus, and receives its event;
4. it reads two preview frames.

With arguments, it is a Thing server with teta-wot's command line:

```
./build/linux-gcc/examples/simulated-microscope/example-simulated-microscope -c examples/simulated-microscope/microscope.json
```

Then:

- <http://127.0.0.1:5000/camera/preview/viewer> shows the live preview (move the stage from <http://127.0.0.1:5000/docs> and watch it change);
- `demo.py` runs a scripted session with a Python client (`pip install labthings-fastapi`, which brings `httpx`):

  ```
  python examples/simulated-microscope/demo.py
  python examples/simulated-microscope/demo.py --spawn build/linux-gcc/examples/simulated-microscope/example-simulated-microscope
  ```

  The first form uses the running server. With `--spawn` (as CTest runs it, `example.simulated-microscope.demo-py`), it starts one on a free port with a temporary settings folder. It saves the captured image as `capture.jpg`. The script is teta-wot's, unchanged, so its comments name Cargo's commands.

## What to look for

- The camera and the autofocus name no Things: their slots are filled by type, and the Things start in the order the slots need (`stage`, `camera`, `autofocus`) whatever the configuration's order.
- The autofocus moves the stage through its action (`stage->call(&Stage::move_to, ...)`), so the moves are validated and logged in the autofocus's invocation, and cancelling the autofocus stops the stage where it is.
- The camera measures through the Thing itself (`camera->thing().measure()`): a plain C++ call.
- `speed` and `exposure` are saved to `{settings}/stage/Settings-Stage.json` and `{settings}/camera/Settings-Camera.json` when they change.
