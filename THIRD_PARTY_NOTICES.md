# Third-party notices

teta-wot-cpp is MIT-licensed ([`LICENSE`](LICENSE)). This repository also holds work by others, under their own licences. The libraries a build links (Drogon and its dependencies, RE2, nlohmann/json, …) come from vcpkg, not from this repository: a release package gathers their licences into `THIRD_PARTY_LICENSES.txt` ([`tools/licenses.py`](tools/licenses.py)).

## Compiled into the library

| What | Where | Licence | When |
|---|---|---|---|
| [Swagger UI](https://github.com/swagger-api/swagger-ui) 5.33.0 | [`assets/swagger-ui-5.33.0/`](assets/swagger-ui-5.33.0/) | Apache-2.0 ([`LICENSE`](assets/swagger-ui-5.33.0/LICENSE), [`NOTICE`](assets/swagger-ui-5.33.0/NOTICE)) | `oauth2-redirect.js` always (inline in `/docs/oauth2-redirect`); the rest with `TETA_WOT_DOCS_OFFLINE` |
| [ReDoc](https://github.com/Redocly/redoc) 2.5.4 | [`assets/redoc-2.5.4/`](assets/redoc-2.5.4/) | MIT ([`LICENSE`](assets/redoc-2.5.4/LICENSE)) | With `TETA_WOT_DOCS_OFFLINE` |
| [mjansson/mdns](https://github.com/mjansson/mdns) 1.4.3 (`mdns.h`) | [`third_party/mdns/`](third_party/mdns/README.md) | Unlicense, public domain ([`LICENSE`](third_party/mdns/LICENSE)) | With `TETA_WOT_MDNS` |

The bundles' own notices for what they bundle are beside them (`*.js.LICENSE.txt`). The assets are stored byte for byte as teta-wot v0.1.0 vendors them ([`assets/README.md`](assets/README.md)); `mdns.h` as its release tarball has it, whose SHA-512 is in its README.

## Used by the tests and tools only

| What | Where | Licence |
|---|---|---|
| The W3C Thing Description 1.1 JSON Schema | [`schema/td-json-schema-validation.json`](schema/td-json-schema-validation.json) | [W3C Software and Document License](https://www.w3.org/copyright/software-license-2023/) |
| What [teta-wot](https://github.com/lucaswewa/teta-wot) v0.1.0 answers and writes: the conformance fixtures, the serde_json, schema, validation and wire corpora, and the fuzz seeds made from them; the simulated microscope's `microscope.json` and `demo.py`; `service_test.ps1` | `conformance/fixtures/`, `tests/**/*corpus*`, `fuzz/seeds/`, `examples/` | MIT, © 2026 Lucas Wang (teta-wot's licence) |
