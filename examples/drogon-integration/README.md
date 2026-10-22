# drogon-integration

**Concept:** serving Things inside an application that already uses Drogon for its own routes, sharing its event loops and leaving its listener and shutdown to it. (An example of the C++ project only: teta-wot has no counterpart.)

**Technologies:** `tw::drogon_adapter::mount` (`<teta/wot/drogon.hpp>`, target `teta::wot-drogon`), `tw::http::HttpBinding` over a `tw::Runtime` the application starts and stops, Drogon's `registerHandler` and `HttpClient`.

## Run it

```
cmake --build --preset linux-gcc --target example-drogon-integration
./build/linux-gcc/examples/drogon-integration/example-drogon-integration            # a short demo on a free port, then exit
./build/linux-gcc/examples/drogon-integration/example-drogon-integration --serve    # serve on http://127.0.0.1:5000 until Ctrl-C
```

It requests the application's route and the Thing's, and exits with a non-zero status if an answer is wrong.

## What to look for

- `/hello` is answered by the application's own handler; everything else reaches the binding, as Drogon's default handler, so an unknown path gets teta-wot's `404` (`{"detail":"Not Found"}`), not Drogon's.
- `mount` changes a few application-wide settings, which the binding needs: no `server` header, a `date` header, a 128 MiB body limit, and the advice that lets streamed bodies run on closing connections.
- The application starts the runtime before `run()` and shuts it down after; `ThingServer::serve` does the same, with a graceful drain, for applications that don't have a Drogon of their own.
