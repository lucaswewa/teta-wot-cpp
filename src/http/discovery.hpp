// Discovery: a port of teta-wot-http's `discovery.rs`. `/.well-known/wot` and
// a read-only Thing Description Directory, as W3C WoT Discovery describes
// them.
//
// - `GET /.well-known/wot` is the directory's own TD (a `ThingDirectory`), at
//   the server's root whatever the API prefix.
// - `GET {prefix}/directory/things` lists the served TDs, sorted by `id`, as
//   `application/ld+json`: an array, or with `format=collection` a
//   `ThingCollection`. `offset` and `limit` page it, with `next` and
//   `canonical` links.
// - `GET {prefix}/directory/things/{id}` is one TD, as `application/td+json`.
//
// Every route also answers `HEAD`; other methods get 405. Errors are problem
// details whatever the wire profile, as the Discovery API wants. The
// directory can't be written to: its Things are the server's.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "render.hpp"
#include "teta/wot/http/binding.hpp"

namespace teta::wot::http::detail {

/// The directory's path under the API prefix: `{prefix}/directory/`.
std::string directory_path(const Urls& urls);

/// The directory's TD: the Discovery specification's Thing Model for a
/// directory, with only its read operations (`things` and `retrieveThing`).
json::Value directory_td(const HttpOptions& options, const Urls& urls);

/// A JSON answer of the given media type.
Response json_as(std::string_view media_type, const json::Value& value);

/// `GET {prefix}/directory/things`, with the request's query.
Response listing(std::vector<json::Value> tds, const std::optional<std::string>& query, const Urls& urls);

/// `GET {prefix}/directory/things/{id}`, with the ID as sent (it may be
/// percent-encoded).
Response retrieve(const std::vector<json::Value>& tds, std::string_view raw_id);

/// Decodes `%XX` escapes (other bytes as they are; invalid UTF-8 becomes
/// U+FFFD, as Rust's `from_utf8_lossy`).
std::string percent_decode(std::string_view raw);

}  // namespace teta::wot::http::detail
