// The interactive API docs: a port of teta-wot-http's `docs.rs`. Swagger UI
// at `/docs` (with `/docs/oauth2-redirect`) and ReDoc at `/redoc`, loading
// their assets from jsDelivr, or, with `TETA_WOT_DOCS_OFFLINE`, from the
// server, which compiles them in.

#pragma once

#include <span>
#include <string_view>

#include "teta/wot/http/message.hpp"

namespace teta::wot::http::detail {

/// A compiled-in asset.
struct DocsAsset {
    std::string_view path;
    std::string_view content_type;
    std::string_view data;
};

/// The compiled-in assets: none without `TETA_WOT_DOCS_OFFLINE`.
std::span<const DocsAsset> docs_assets();

/// Escapes text for HTML.
std::string escape_html(std::string_view text);

/// Swagger UI, titled after the API.
Response swagger_ui(std::string_view title);
/// Swagger UI's OAuth2 redirect page, with its script inline.
Response oauth2_redirect();
/// ReDoc, titled after the API.
Response redoc(std::string_view title);
/// A compiled-in asset, cached for a day.
Response docs_asset(const DocsAsset& asset);

}  // namespace teta::wot::http::detail
