# docs-offline

**Concept:** the interactive API documentation on a lab network without internet access. Built with `TETA_WOT_DOCS_OFFLINE=ON`, Swagger UI (`/docs`) and ReDoc (`/redoc`) are compiled into the server, so their pages load nothing from outside.

**Technologies:** the CMake option `TETA_WOT_DOCS_OFFLINE` (teta-wot's `docs-offline` feature); Swagger UI 5.33.0 and ReDoc 2.5.4, vendored in `assets/`; `ThingServerBuilder::api_info`. The C++ port of teta-wot's `docs-offline`.

## Run it

The option is off by default, as teta-wot's feature is; the preset `windows-msvc-docs-offline` turns it on (or configure with `-DTETA_WOT_DOCS_OFFLINE=ON`):

```
cmake --preset windows-msvc-docs-offline
cmake --build --preset windows-msvc-docs-offline --target example-docs-offline
.\build\windows-msvc-docs-offline\examples\docs-offline\example-docs-offline.exe            # an in-process check, then exit
.\build\windows-msvc-docs-offline\examples\docs-offline\example-docs-offline.exe --serve    # serve on http://127.0.0.1:5000
```

Built without the option, the in-process check has nothing to check: it says so and exits 77, which CTest reports as skipped.

While it serves, open <http://127.0.0.1:5000/docs> or <http://127.0.0.1:5000/redoc>, even with the network unplugged.

## What to look for

- The pages refer only to the server: `/docs/swagger-ui-bundle.js`, `/docs/swagger-ui.css`, `/docs/favicon-32x32.png` and `/redoc/redoc.standalone.js`, each cached for a day.
- The pages' titles are the API's (`Lamp`), and "Try it out" in Swagger UI calls the Lamp's routes on the same server.
