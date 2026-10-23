# Blobs and streams

JSON can't carry an image or a data file, so actions take and return them as Blobs, which clients download; and a camera's live view is an MJPEG stream, which a browser plays in an `<img>`.

## Blobs

A `Blob<M>` is a handle to binary data of the media type `M`: `tw::blob::Jpeg`, `Png`, `Tiff`, `TextPlain`, `Csv`, `Json`, `OctetStream`, `AnyMedia` (`*/*`), or one of your own:

```cpp
struct Spectrum {
    static constexpr std::string_view media_type = "text/csv";
    static constexpr std::string_view title = "SpectrumBlob";
    static constexpr std::string_view description = "A spectrum, one wavelength per line.";  // optional
};
```

An action returns one, or takes one as a parameter, like any other type:

```cpp
tw::Blob<tw::blob::Jpeg> capture() { return tw::Blob<tw::blob::Jpeg>::from_bytes(take_picture()); }

ImageInfo describe(tw::Blob<tw::blob::Jpeg> image) {
    const std::string data = image.bytes();
    ...
}
```

The data can be bytes in memory (`from_bytes`), a file that stays where it is (`from_file`), a file in a temporary directory that is deleted with the data (`from_temporary_directory`, the safe way to return a file made for the purpose), or data elsewhere (`from_url`). `from_bytes_as` and `from_file_as` give a more specific media type than `M`'s, such as `text/csv` for a `text/*` Blob.

In JSON, a Blob is a link:

```json
{"href": "http://host/blob/2b5f…", "media_type": "image/jpeg", "rel": "output", "description": "…"}
```

`GET {prefix}/blob/{id}` downloads the data, with its media type (`; charset=utf-8` added to text). An invocation whose output is one Blob serves the data itself at its `/output`, or redirects to a remote Blob's URL. To pass a Blob back to an action, a client sends its JSON form: the `href` is what counts, and a Blob that isn't on the server, or isn't of the parameter's media type, is refused (422) before the action runs.

**Lifetime.** Data can be downloaded for as long as something holds it. An invocation holds the Blobs of its input and output until it expires, five minutes after it finished by default (`.retention(...)`), so a Blob output can be downloaded for that long; when the last handle goes, the data is freed and a temporary directory deleted.

## MJPEG streams

A Thing declares an `MjpegStream` member, and a thread of its own feeds it:

```cpp
class Camera {
  public:
    tw::MjpegStream preview;

    void start_camera() {
        thread_ = std::jthread([this, preview = preview](std::stop_token stop) {
            while (!stop.stop_requested()) {
                preview.add_frame(render());  // never waits
                ...
            }
        });
    }

    static void define(tw::ThingSpec<Camera>& t) {
        t.stream("preview", &Camera::preview);
        t.on_start(&Camera::start_camera);
        ...
    }
};
```

`GET /camera/preview` is the stream (`multipart/x-mixed-replace`), and `/camera/preview/viewer` a page that shows it; the TD links to both. `add_frame` checks the JPEG markers and never waits: each client gets the latest frame whenever it is ready for one, so a slow client skips frames and never holds up the camera or the others. The last frames (ten by default) are kept by index (`frame(i)`, `latest_frame()`); `next_frame`, `grab_frame` and `next_frame_size` wait for the next one, until a `std::stop_token` you pass asks them to stop. `stop()` ends every client's stream, and `reset()` starts again.

In tests, `TestClient::stream` reads a stream chunk by chunk; the examples `blob-camera` and `mjpeg-stream` show both.
