# openapi-export

**Concept:** describing a server's API without running it. The example builds a runtime (it doesn't start it) and writes the OpenAPI 3.1 document and every Thing Description to files. A standard code generator then makes a TypeScript client from the document.

**Technologies:** `tw::http::openapi` and `tw::http::thing_description`; `tw::http::HttpOptions` (the API's title and version); `openapi-typescript`, run with `npx`. The C++ port of teta-wot's `openapi-export`.

## Run it

```
cmake --build --preset linux-gcc --target example-openapi-export
./build/linux-gcc/examples/openapi-export/example-openapi-export                                     # writes to ./openapi-export
./build/linux-gcc/examples/openapi-export/example-openapi-export out --base http://lab-pc:5000/      # another folder, TDs for another host
```

It writes `openapi.json`, `stage.td.json` and `camera.td.json`. Then generate the client's types (Node.js needed):

```
npx --yes openapi-typescript@7 openapi-export/openapi.json -o openapi-export/api.d.ts
```

With [`openapi-fetch`](https://openapi-ts.dev/openapi-fetch/), the types make a typed client:

```ts
import createClient from "openapi-fetch";
import type { paths } from "./api";

const client = createClient<paths>({ baseUrl: "http://localhost:5000" });
const { data } = await client.POST("/stage/move_to", { body: { x: 1, y: 2 } });
```

## What to look for

- Nothing runs and nothing listens: the document and the TDs come from the Things' definitions alone.
- The `operationId`s are FastAPI's (`start_action_stage_move_to_post`), so clients generated against teta-wot or LabThings keep working.
- The document is byte for byte what teta-wot's example writes (CTest's `conformance.openapi-export` compares them).
