# blob-camera

**Concept:** Blobs, for binary data that JSON can't carry. A simulated camera returns its captures as JPEG Blobs, one from memory and one from a file in a temporary directory. Clients download them from `/blob/{id}`, and pass them back as the input of another action.

**Technologies:** `tw::Blob<tw::blob::Jpeg>` as an action's output and input; `Blob::from_bytes` and `Blob::from_temporary_directory` with a `tw::blob::TemporaryDirectory`; `image.bytes()`; the examples' small JPEG encoder (`examples/common/jpeg.hpp`) for the simulated images. The C++ port of teta-wot's `blob-camera`.

## Run it

```
cmake --build --preset linux-gcc --target example-blob-camera
./build/linux-gcc/examples/blob-camera/example-blob-camera            # an in-process demo, then exit
./build/linux-gcc/examples/blob-camera/example-blob-camera --serve    # serve on http://127.0.0.1:5000
```

While it serves, capture an image (on Windows, write `curl.exe`):

```
curl -X POST http://127.0.0.1:5000/camera/capture
```

The answer's `links` include the invocation's `output`; once it has finished, `GET` it, or the Blob's own `href` in the invocation's `output`, to download the JPEG:

```
curl -o capture.jpg http://127.0.0.1:5000/action_invocations/<id>/output
```

## What to look for

- An action whose output is one Blob: its invocation's `output` is the Blob's JSON form (`href`, `media_type`, `rel`, `description`), and its `/output` is the data itself, with the media type.
- The `href` is absolute, with the request's host (and the API prefix, if there is one).
- `describe` takes the Blob back: only its `href` matters, which is how the server finds the data. A Blob that isn't there, or of another media type, is refused with a 422 before the action runs.
- The temporary directory of `capture_to_file` is deleted with the Blob's data, when the invocation expires (five minutes after it finished) and nothing else holds the Blob.
