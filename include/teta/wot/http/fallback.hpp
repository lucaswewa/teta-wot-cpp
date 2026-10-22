// The fallback server: when the server can't start, an HTTP error page is
// served instead, so that a headless instrument still explains itself at its
// usual address. A port of teta-wot-http's `fallback`.
//
// It answers `/` with the page (500), `/fallback` with `true` (so clients can
// tell it from the real server), and redirects every other path to `/` (307).
// CORS is as the binding's.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "teta/wot/http/message.hpp"

namespace teta::wot::http {

/// What the fallback page shows.
struct FallbackPage {
    /// Why the server couldn't start.
    std::string error_message;
    /// The Things that were built, if any.
    std::vector<std::string> things;
    /// The configuration, as text.
    std::string config;
    /// The error and its causes.
    std::string traceback;
    /// Log output, if any was captured.
    std::optional<std::string> logging;

    /// The page's HTML.
    std::string html() const;
};

/// Escapes text for HTML (`&`, `<`, `>`, `"` and `'`).
std::string escape_html(std::string_view text);

/// The fallback server's answer to `request`, given the page's HTML.
Response fallback_answer(const std::string& html, const Request& request);

}  // namespace teta::wot::http
