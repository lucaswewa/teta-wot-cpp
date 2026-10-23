# The API document and docs

Every server describes its HTTP API twice: each Thing's Thing Description, for Web of Things clients, and an OpenAPI 3.1 document, for everything else.

## The OpenAPI document

`GET /openapi.json` is the document, at the root whatever the API prefix (its paths carry the prefix, and it has no `servers`, so clients resolve them against its own host). It is teta-wot's document, byte for byte: the same paths and schemas, and the same `operationId`s, which are FastAPI's (`get_property_counter_counter_get`, `start_action_stage_move_to_post`), so clients generated for LabThings or teta-wot keep working. Its title and version are the server's:

```cpp
auto server = tw::ThingServer::builder().thing<Lamp>("lamp").api_info("Lamp", "1.0.0").build();
```

A custom endpoint is named after the function that answers it in teta-wot; in C++, give it that name, or its path is used:

```cpp
t.endpoint("GET", "readings.csv", &Logger::readings_csv).name("readings_csv").doc("The readings as a CSV file.");
```

The document doesn't depend on the request, nor on the Things having started, so it can be written without a server, for a code generator (the `openapi-export` example):

```cpp
const auto runtime = tw::Runtime::builder().thing<Stage>("stage").build();  // not started
tw::http::HttpOptions options;
options.api_title = "Microscope";
std::ofstream("openapi.json") << tw::json::dump_pretty(tw::http::openapi(runtime, options)) << "\n";
```

`tw::http::thing_description(runtime, "stage", "http://lab-pc:5000/", options)` writes a TD the same way.

## Swagger UI and ReDoc

`/docs` is Swagger UI, where each route can be tried, and `/redoc` is ReDoc; both read `/openapi.json`. They load Swagger UI 5.33.0 and ReDoc 2.5.4 from jsDelivr. On a network without internet, build teta-wot-cpp with `-DTETA_WOT_DOCS_OFFLINE=ON` (the preset `windows-msvc-docs-offline` does): the assets are compiled in, and the pages load them from the server (the `docs-offline` example).
