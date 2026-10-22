#include "teta/wot/http/fallback.hpp"

#include "render.hpp"

namespace teta::wot::http {

std::string escape_html(std::string_view text) {
    std::string out;
    out.reserve(text.size());
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
            case '\'':
                out += "&#x27;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

std::string FallbackPage::html() const {
    std::string html =
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\" />\n"
        "<title>teta-wot: the server failed to start</title>\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
        "<style>\nbody { font-family: system-ui, sans-serif; margin: 2rem; line-height: 1.5; color: #1f2933; "
        "}\n"
        "main { max-width: 900px; margin: 0 auto; }\n"
        ".error { padding: 1rem; background: #fee2e2; border: 1px solid #fecaca; border-radius: 6px; color: "
        "#7f1d1d; }\n"
        "pre { background: #f1f5f9; padding: 1rem; border-radius: 6px; white-space: pre-wrap; word-break: "
        "break-word; }\n"
        ".muted { color: #6b7280; }\n</style>\n</head>\n<body>\n<main>\n"
        "<h1>The server couldn't start</h1>\n"
        "<p class=\"muted\">The server failed during startup. This is usually caused by an invalid "
        "configuration, a missing Thing type, or an error while a Thing started.</p>\n";
    if (!error_message.empty()) {
        html += "<div class=\"error\">" + escape_html(error_message) + "</div>\n";
    }
    if (!things.empty()) {
        html += "<h2>The following Things loaded successfully:</h2>\n<ul>\n";
        for (const auto& thing : things) {
            html += "<li>" + escape_html(thing) + "</li>\n";
        }
        html += "</ul>\n";
    }
    html += "<h2>Configuration</h2>\n<pre>" + escape_html(config) + "</pre>\n<h2>Error</h2>\n<pre>" +
            escape_html(traceback) + "</pre>\n<h2>Logging</h2>\n";
    if (logging && !logging->empty()) {
        html += "<pre>" + escape_html(*logging) + "</pre>\n";
    } else {
        html += "<p class=\"muted\">No logging information available.</p>\n";
    }
    html += "</main>\n<footer class=\"muted\">teta-wot fallback server</footer>\n</body>\n</html>\n";
    return html;
}

Response fallback_answer(const std::string& html, const Request& request) {
    std::optional<std::string> origin;
    if (const auto value = request.headers.get("origin")) {
        origin = std::string(*value);
    }
    // A CORS preflight is answered on any path.
    if (origin && request.method == "OPTIONS" && request.headers.contains("access-control-request-method")) {
        return detail::preflight(*origin, request.headers.get("access-control-request-headers"));
    }
    Response response;
    if (request.path == "/") {
        response = Response::bytes(html, "text/html; charset=utf-8", 500);
    } else if (request.path == "/fallback") {
        response = Response::bytes("true", "application/json");
    } else {
        response = Response::empty(307);
        response.headers.set("location", "/");
    }
    detail::add_cors(response, origin);
    return response;
}

}  // namespace teta::wot::http
