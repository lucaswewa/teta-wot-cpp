// The OpenAPI 3.1 document and the interactive API docs: a port of
// teta-wot-http's `openapi.rs` and `docs.rs`.
//
// The binding serves the document at `/openapi.json`, Swagger UI at `/docs`
// (with `/docs/oauth2-redirect`) and ReDoc at `/redoc`, at the root as
// FastAPI does. The pages load Swagger UI and ReDoc from jsDelivr, pinned to
// the versions below; built with the CMake option `TETA_WOT_DOCS_OFFLINE`,
// the assets are compiled in and served from `/docs/…` and `/redoc/…`, so
// the pages work without network access.

#pragma once

#include <string>
#include <string_view>

#include "teta/wot/http/binding.hpp"
#include "teta/wot/json.hpp"
#include "teta/wot/runtime.hpp"

namespace teta::wot::http {

/// The OpenAPI 3.1 document for a runtime's Things, as served at
/// `/openapi.json`. It doesn't depend on the request, nor on the runtime
/// having started, so it can be written to a file without a server (the
/// `openapi-export` example). Paths carry the API prefix and there is no
/// `servers` entry, so clients resolve them against the document's host.
json::Value openapi(const Runtime& runtime, const HttpOptions& options);

/// The `operationId` of a route: its name and path, with every character
/// that isn't a letter, a digit or `_` replaced by `_`, then `_` and the
/// lower-case method, as FastAPI's `generate_unique_id` makes it
/// (`operation_id("get_property", "/mything/foo", "GET")` is
/// `get_property_mything_foo_get`).
std::string operation_id(std::string_view name, std::string_view path, std::string_view method);

/// The Swagger UI release the docs pages use.
inline constexpr std::string_view swagger_ui_version = "5.33.0";
/// The ReDoc release the docs pages use.
inline constexpr std::string_view redoc_version = "2.5.4";

/// Whether the docs assets are compiled in (`TETA_WOT_DOCS_OFFLINE`).
bool docs_offline();

}  // namespace teta::wot::http
