# mjpeg-stream

**Concept:** MJPEG streams. A simulated camera thread feeds JPEG frames into a stream, which any number of browsers play in an `<img>`. Actions wait for frames: one measures the next frame's size, an autofocus sharpness measure, and another saves a frame as a Blob.

**Technologies:** a `tw::MjpegStream` member declared with `t.stream(...)`; `add_frame` from a plain `std::jthread`; `next_frame_size` and `grab_frame`, cancelled with the invocation through a `std::stop_token`; `multipart/x-mixed-replace`; `t.on_start` and `t.on_stop`; `TestClient::stream`; the examples' small JPEG encoder (`examples/common/jpeg.hpp`) for the frames. The C++ port of teta-wot's `mjpeg-stream`.

## Run it

```
cmake --build --preset linux-gcc --target example-mjpeg-stream
./build/linux-gcc/examples/mjpeg-stream/example-mjpeg-stream            # an in-process demo, then exit
./build/linux-gcc/examples/mjpeg-stream/example-mjpeg-stream --serve    # serve on http://127.0.0.1:5000
```

While it serves, open <http://127.0.0.1:5000/camera/preview/viewer>. The bar sweeps across, and the counter along the top counts frames in binary. The stream itself is at <http://127.0.0.1:5000/camera/preview>. Change the frame rate with `curl -X PUT -H "Content-Type: application/json" -d 25 http://127.0.0.1:5000/camera/fps` (on Windows, write `curl.exe`).

## What to look for

- `add_frame` never waits, however many clients there are or however slow they are: each client gets the latest frame whenever it is ready for one, and a slow one skips frames.
- The TD links to the stream and its viewer page (`"rel": "alternate"`).
- `sharpness` and `snapshot` wait for the next frame; cancelling the invocation, or stopping the camera, ends the wait.
- Stopping the server stops the camera thread and ends every client's stream.
