# Phase 7: Blobs and MJPEG

Status: **done** (2026-10-23). Plan: [§5, Phase 7](../PLAN.md#phase-7-blobs-and-mjpeg-m). Actions take and return binary data as Blobs, which clients download, and Things serve MJPEG streams with a viewer page, as teta-wot's do.

## What was built

| Deliverable | Where |
|---|---|
| `Blob<M>`, a cheap handle to data in memory, in a file, in a file of a temporary directory, or at a URL, of the media type `M` (`blob::Jpeg`, `Png`, `Tiff`, `TextPlain`, `Csv`, `Json`, `OctetStream`, `AnyMedia`, or an application's own); media types parsed and matched with wildcards; the registry of weak references that downloads find data in; `TemporaryDirectory`, deleted with the data | `blob.hpp`, `src/core/blob.cpp` |
| The JSON form and its schema (`Codec<Blob<M>>`): written with a relative `blob/<id>`; read by the ID in the `href`, as serde derives teta-wot's `BlobModel` | `blob.hpp`, `src/core/blob.cpp` |
| `ValueDecodeError`: a decode error that is pydantic's `value_error`, with its path in the input, which containers and structs extend (`codec::at`); an action's input or a property write refused with it at the Blob's location (`ValidationError::value_error_at`) | `codec.hpp`, `validate.hpp`, `thing.hpp`, `property.hpp` |
| Invocations keep their Blobs: the data of the Blobs read from the input and written in the output (`blob::Capture`) is held until the invocation expires; `Invocation::output_blob` when the output is one Blob | `invocation.hpp`, `src/core/runtime.cpp` |
| `MjpegStream` (a port of teta-wot-core's `stream`): `add_frame` from any thread, checking the JPEG markers and never waiting; a ring buffer by index; `next_frame`, `grab_frame` and `next_frame_size`, stopped by a `std::stop_token`; `stop` and `reset`; `FrameReceiver`, the latest frame for each client, with a notifier for consumers that don't wait | `stream.hpp`, `src/core/stream.cpp` |
| `t.stream(name, &T::member)`; streams in the TD's links | `thing.hpp`, `runtime.hpp` |
| HTTP: `GET {prefix}/blob/{id}`; a Blob output served (or redirected to) at `/output`; `blob/<id>` hrefs made absolute in every JSON answer, WebSocket message and server-sent event; the MJPEG answer, driven by the receiver's notifier and the sink's readiness (no thread per client); the viewer page | `src/http/output.hpp`, `src/http/output.cpp`, `src/http/binding.cpp`, `src/http/routes.cpp`, `src/http/sse.cpp`, `src/http/websocket.cpp` |
| `TestClient::stream` and `TestStream`, a streamed answer read chunk by chunk | `testing.hpp`, `src/server/testing.cpp` |
| `BlobProbe`, the `wot` repository's Blob and stream twin, in C++ | `conformance/twins/` |
| `run.py`: `{blob:N}` (the N-th Blob a JSON answer linked to), an MJPEG client (`mjpeg`, and `stream` steps with `frames`, `quiet`, `ended`), and `ignore_body` | `conformance/run.py` |
| Scenarios and fixtures: `blobs`, `blobs-wot`, `streams`; example scenarios `blob-camera`, `mjpeg-stream` | `conformance/scenarios/`, `conformance/fixtures/` |
| Two examples, `blob-camera` and `mjpeg-stream`, and a small baseline JPEG encoder for their simulated cameras | `examples/` |

## The gate

- **The Blob and stream scenarios equal the reference's** (`conformance.http`): `blobs` (outputs from memory, a temporary file, text, CSV and a map of two; `/output` and downloads with their media types and charsets; Blob inputs found by absolute, foreign and relative `href`s; every refused input: the wrong media type, an unknown or malformed ID, no ID, missing fields, a number, a string, a list; unknown and malformed download IDs; `HEAD` and `POST`), `blobs-wot` (the `wot` profile under a prefix) and `streams` (the viewer page; frames to one and two clients; nothing more until a frame is added; a stopped stream ending its clients; a stream opened while stopped; a restart; the end at shutdown; 404 and 405). `blob_probe`'s TD equals the reference's in both profiles (`TwinTds`, eight twins of nine).
- **The examples**: `blob-camera` and `mjpeg-stream` print what their Rust originals print (but for IDs and the JPEGs' sizes) and, served, answer their TD and a scripted session as teta-wot's examples do (`conformance.examples`), the JPEG bodies aside.
- **teta-wot-core's `blob.rs` and `stream.rs` tests, and teta-wot-http's `output.rs` test, are ported**, with tests of serde's `BlobModel` shapes, captures, invocations keeping and freeing their Blobs, a bad Blob's `value_error` in a list, receivers' notifiers, downloads, redirects, a file that can't be read, the viewer page, and the MJPEG feed with a slow client (32 in all).
- 359 tests pass with `windows-msvc`. Not run in this phase: the Linux presets and the sanitizer builds, which CI has off for now.

## Findings

- **A Blob input's shape is checked by its schema first**: missing fields, a non-string `href`, a `media_type` other than the `const`, a string or a list are pydantic's errors at their field; only finding the data is a `value_error`, at the Blob's location, with the whole Blob object as its input. teta-wot's `BlobModel` (serde-derived) would also read a list `[href, media_type]`, but the schema refuses one before it gets there; the C++ codec reads it too, for direct use.
- **The `href` is searched, not parsed**: `blob/<id>` anywhere in it finds the data, so a Blob passed back with another host or prefix (or none) is found; the ID may be hyphenated or not.
- **In the `wot` profile, a malformed download ID is a 404 `Blob not found`**, where `tetathing` answers 422 `uuid_parsing`.
- **A stream opened while stopped answers 200 and ends at once**; a frame added to a stopped stream is kept (the action succeeds) but sent to no one.
- **Rust's `usize` has no C++ twin**, but the TD drops numeric formats, so an unsigned C++ integer gives the same DataSchema (`minimum: 0`); `mjpeg-stream`'s `sharpness` returns `std::uint64_t` for that.
- Two bugs found by the scenarios before they reached a commit: the MJPEG feed had no owner once the transport had started it (it now owns itself until its stream ends), and a stream's notifiers were dropped after their first call.

## Deviations from the plan

- **The examples' JPEG encoder is the project's own** (`examples/common/jpeg.hpp`, baseline, 4:4:4, the specification's tables), not a vendored `stb_image_write`: about 250 lines, with nothing third-party to vendor. Its images decode as intended (checked with .NET's decoder), but they aren't `jpeg-encoder`'s bytes, so the example scenarios don't compare JPEG bodies (`ignore_body`), nor anything that depends on their size.
- **An MJPEG client that goes away is noticed at the next frame** (when sending fails), or when the stream stops or the server shuts down; a quiet stream keeps its feed until then.
