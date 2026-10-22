# custom-endpoint

**Concept:** HTTP routes of a Thing that aren't affordances: a file download, plain text, and a `POST` of raw text. They are served at `/{thing}/{path}` but aren't in the Thing Description.

**Technologies:** `ThingSpec::endpoint(method, path, handler)` with member functions taking a `tw::http::Request` (or nothing) and returning a `tw::http::Response` (`Response::bytes`, `Response::text`, `Response::empty`); `.doc()`, and `.link()` to list one in the TD. The C++ port of teta-wot's `custom-endpoint`, where the handlers are axum handlers with extractors: here the handler reads the transport-neutral request itself.

## Run it

```
cmake --build --preset linux-gcc --target example-custom-endpoint
./build/linux-gcc/examples/custom-endpoint/example-custom-endpoint
```

It downloads the CSV, reads the summary in both units, posts a note, prints the same lines as the Rust example, and exits with a non-zero status if an answer is wrong.

## What to look for

- An endpoint chooses its own status, headers and body; answers get CORS's `vary` like any other.
- Endpoints run on a worker thread, as all Thing code does, so they may block.
- A method the endpoint doesn't answer is a `405` with `allow`, as for any route.
