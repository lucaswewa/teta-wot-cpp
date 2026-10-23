#include "docs.hpp"

#include <array>
#include <cstddef>
#include <string>

#include "teta/wot/http/openapi.hpp"

namespace teta::wot::embedded {
extern const unsigned char swagger_oauth2_redirect_js[];
extern const std::size_t swagger_oauth2_redirect_js_size;
#ifdef TETA_WOT_DOCS_OFFLINE
extern const unsigned char swagger_ui_bundle_js[];
extern const std::size_t swagger_ui_bundle_js_size;
extern const unsigned char swagger_ui_css[];
extern const std::size_t swagger_ui_css_size;
extern const unsigned char swagger_favicon_png[];
extern const std::size_t swagger_favicon_png_size;
extern const unsigned char redoc_standalone_js[];
extern const std::size_t redoc_standalone_js_size;
#endif
}  // namespace teta::wot::embedded

namespace teta::wot::http {

bool docs_offline() {
#ifdef TETA_WOT_DOCS_OFFLINE
    return true;
#else
    return false;
#endif
}

namespace detail {

namespace {

std::string_view text(const unsigned char* data, std::size_t size) {
    return {reinterpret_cast<const char*>(data), size};
}

/// Where a page loads an asset from: the server, or jsDelivr.
std::string asset_url(std::string_view offline_path, std::string_view package, std::string_view file) {
    if (docs_offline()) {
        return std::string(offline_path);
    }
    return "https://cdn.jsdelivr.net/npm/" + std::string(package) + "/" + std::string(file);
}

std::string swagger(std::string_view file) {
    return asset_url("/docs/" + std::string(file), "swagger-ui-dist@" + std::string(swagger_ui_version),
                     file);
}

Response html(std::string body) {
    return Response::bytes(std::move(body), "text/html; charset=utf-8");
}

}  // namespace

std::span<const DocsAsset> docs_assets() {
#ifdef TETA_WOT_DOCS_OFFLINE
    static const std::array<DocsAsset, 4> assets = {
        DocsAsset{"/docs/swagger-ui-bundle.js", "text/javascript; charset=utf-8",
                  text(embedded::swagger_ui_bundle_js, embedded::swagger_ui_bundle_js_size)},
        DocsAsset{"/docs/swagger-ui.css", "text/css; charset=utf-8",
                  text(embedded::swagger_ui_css, embedded::swagger_ui_css_size)},
        DocsAsset{"/docs/favicon-32x32.png", "image/png",
                  text(embedded::swagger_favicon_png, embedded::swagger_favicon_png_size)},
        DocsAsset{"/redoc/redoc.standalone.js", "text/javascript; charset=utf-8",
                  text(embedded::redoc_standalone_js, embedded::redoc_standalone_js_size)}};
    return assets;
#else
    return {};
#endif
}

std::string escape_html(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

Response swagger_ui(std::string_view title) {
    return html(R"(<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<link type="text/css" rel="stylesheet" href=")" +
                swagger("swagger-ui.css") + R"(">
<link rel="shortcut icon" href=")" +
                swagger("favicon-32x32.png") + R"(">
<title>)" + escape_html(title) +
                R"( - Swagger UI</title>
</head>
<body>
<div id="swagger-ui">
</div>
<script src=")" +
                swagger("swagger-ui-bundle.js") +
                R"("></script>
<script>
const ui = SwaggerUIBundle({
    url: '/openapi.json',
    dom_id: '#swagger-ui',
    layout: 'BaseLayout',
    deepLinking: true,
    showExtensions: true,
    showCommonExtensions: true,
    oauth2RedirectUrl: window.location.origin + '/docs/oauth2-redirect',
    presets: [
        SwaggerUIBundle.presets.apis,
        SwaggerUIBundle.SwaggerUIStandalonePreset
    ],
})
</script>
</body>
</html>
)");
}

Response oauth2_redirect() {
    return html(
        "<!doctype html>\n<html lang=\"en-US\">\n<head>\n<title>Swagger UI: OAuth2 "
        "Redirect</title>\n</head>\n<body>\n"
        "<script>\n" +
        std::string(text(embedded::swagger_oauth2_redirect_js, embedded::swagger_oauth2_redirect_js_size)) +
        "\n</script>\n</body>\n</html>\n");
}

Response redoc(std::string_view title) {
    return html(R"(<!DOCTYPE html>
<html>
<head>
<title>)" + escape_html(title) +
                R"( - ReDoc</title>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="shortcut icon" href=")" +
                swagger("favicon-32x32.png") + R"(">
<style>
  body {
    margin: 0;
    padding: 0;
  }
</style>
</head>
<body>
<noscript>
    ReDoc requires Javascript to function. Please enable it to browse the documentation.
</noscript>
<redoc spec-url="/openapi.json"></redoc>
<script src=")" +
                asset_url("/redoc/redoc.standalone.js", "redoc@" + std::string(redoc_version),
                          "bundles/redoc.standalone.js") +
                R"("> </script>
</body>
</html>
)");
}

Response docs_asset(const DocsAsset& asset) {
    Response response = Response::bytes(std::string(asset.data), std::string(asset.content_type));
    response.headers.set("cache-control", "public, max-age=86400");
    return response;
}

}  // namespace detail

}  // namespace teta::wot::http
