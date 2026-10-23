// Describing a Thing: the Thing Description assembled from what a Thing has
// (its affordances' names, docs, schemas and options), as teta-wot's runtime
// assembles it (`ThingHandle::thing_description`). The runtime (Phase 3)
// fills a ThingInfo from a Thing's definition; tests and tools can fill one
// by hand.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"
#include "teta/wot/td/thing.hpp"

namespace teta::wot::td {

/// A title and a description from a docstring, as teta-wot's
/// `split_docstring`: the title is the first line, trimmed; the description
/// is the rest if there are more than two lines and the second is blank,
/// otherwise the whole docstring.
std::pair<std::string, std::string> split_docstring(std::string_view doc);

/// A Thing's stable TD `id`: `urn:uuid:` and the UUIDv5 of
/// `{server_id}/{thing}` in teta-wot's namespace (the UUIDv5 of
/// `https://github.com/lucaswewa/wot#td-id` in the URL namespace).
std::string thing_id(std::string_view server_id, std::string_view thing);

/// The UUIDv5 of `name` in the namespace `ns` (16 bytes), as text.
std::string uuid_v5(const std::array<unsigned char, 16>& ns, std::string_view name);
/// The UUIDv5 of `name` in RFC 4122's URL namespace, as text.
std::string url_uuid_v5(std::string_view name);

/// The media type of a Blob, if `schema` is a Blob's DataSchema: an object
/// with an `href`, a `media_type` whose default is the media type, and a
/// `rel` fixed to `output`.
std::optional<std::string> blob_media_type(const DataSchema& schema);

/// The media type of MJPEG streams.
inline constexpr const char* mjpeg_media_type = "multipart/x-mixed-replace";

/// A property, as the TD describes it.
struct PropertyInfo {
    std::string name;
    std::string title;
    std::optional<std::string> description;
    /// Its type's DataSchema, with its constraints applied.
    DataSchema schema;
    bool read_only = false;
    /// Data properties are observable; functional ones aren't.
    bool observable = false;
    std::optional<json::Value> default_value;
    std::optional<std::string> unit;
    std::vector<std::string> semantic_types;
};

/// An action, as the TD describes it.
struct ActionInfo {
    std::string name;
    std::string title;
    std::optional<std::string> description;
    DataSchema input;
    DataSchema output;
    /// Served synchronously in the `wot` profile.
    bool synchronous = false;
    std::vector<std::string> semantic_types;
};

/// An event, as the TD describes it.
struct EventInfo {
    std::string name;
    std::string title;
    std::optional<std::string> description;
    /// The data's schema; none for events without data.
    std::optional<DataSchema> data;
    std::vector<std::string> semantic_types;
};

/// A custom endpoint that the TD links to.
struct EndpointLink {
    /// Its path below the Thing's.
    std::string path;
    std::string rel;
    std::optional<std::string> media_type;
};

/// What a Thing has, for its TD. Affordances are in definition order (the TD
/// sorts them).
struct ThingInfo {
    std::string title;
    std::optional<std::string> description;
    std::vector<std::string> semantic_types;
    std::vector<std::pair<std::string, std::string>> context_prefixes;
    std::vector<PropertyInfo> properties;
    std::vector<ActionInfo> actions;
    std::vector<EventInfo> events;
    /// The names of its MJPEG streams.
    std::vector<std::string> streams;
    std::vector<EndpointLink> endpoint_links;
};

/// How a TD is assembled: teta-wot's `TdOptions`.
struct TdOptions {
    /// The Thing's path, with the trailing slash: `/{api_prefix}/{name}/`.
    /// Form `href`s are this path followed by the affordance's name.
    std::string path;
    std::optional<std::string> base;
    std::optional<std::string> id;
    /// Describe observation and events: `observable` data properties with
    /// SSE forms (and WebSocket forms, given the URL), and the events.
    bool observation = false;
    /// The Thing's WebSocket URL, for WebSocket forms next to the SSE ones.
    std::optional<std::string> websocket;
    /// List MJPEG streams, their viewer pages and linked endpoints in `links`.
    bool links = false;
    /// Add the top-level forms.
    bool top_level = false;
    /// Where invocations are served, such as `/action_invocations/`: every
    /// action then says whether it is synchronous, and asynchronous ones get
    /// `queryaction`/`cancelaction` forms (and a download form for a Blob).
    std::optional<std::string> invocations;
    /// Whether synchronous actions are served synchronously (`wot` profile).
    bool synchronous_actions = false;
    std::vector<std::string> profiles;
    /// The security scheme every form requires, with its name.
    std::optional<std::pair<std::string, SecurityScheme>> security;
};

/// The Thing Description: properties, actions and events in alphabetical
/// order, forms at `{path}{name}`, `readOnly` and `writeOnly` always
/// written, action schemas titled `<name>_input` and `<name>_output`, and
/// the options' additions. Throws TdError.
ThingDescription describe(const ThingInfo& thing, const TdOptions& options);

}  // namespace teta::wot::td
