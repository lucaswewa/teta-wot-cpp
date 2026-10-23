// Rendering answers: absolute URLs, invocations, errors in each wire profile,
// and CORS. Ports of teta-wot-http's `render.rs`, `problem.rs` and `cors.rs`.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "teta/wot/http/binding.hpp"
#include "teta/wot/http/message.hpp"
#include "teta/wot/invocation.hpp"
#include "teta/wot/problem.hpp"
#include "teta/wot/validate.hpp"

namespace teta::wot::http::detail {

/// The URLs of the request being answered: absolute URLs are built from its
/// scheme and host when they are written, never stored.
struct Urls {
    /// `scheme://host[:port]`, without a trailing slash.
    std::string origin;
    /// The API prefix, such as `/api/v1`, or empty.
    std::string prefix;

    /// The scheme of an absolute-form target (or `http`) and the `Host`
    /// header (or the target's authority, or `localhost`).
    static Urls from_request(const Request& request, const std::string& prefix);
    /// The URLs of a server at `base` (`scheme://host[:port]/`).
    static Urls from_base(std::string_view base, const std::string& prefix);

    /// The TD's `base`: `scheme://host/`.
    std::string base() const { return origin + "/"; }
    /// The absolute URL of a path on this server (the path has the prefix).
    std::string absolute(std::string_view path) const { return origin + std::string(path); }
    std::string thing_path(std::string_view thing) const { return prefix + "/" + std::string(thing) + "/"; }
    /// A Thing's WebSocket: `ws://` (or `wss://`) and the request's host.
    std::string websocket(std::string_view thing) const;
    std::string invocation_href(const Uuid& id) const;
};

/// A local time without its zone, with microseconds (left out when zero), as
/// teta-wot writes invocation times.
std::string naive_local(std::chrono::system_clock::time_point time);
/// A UTC time in RFC 3339 with exactly `digits` fractional digits (6 or 9).
std::string rfc3339(std::chrono::system_clock::time_point time, int digits);

/// Makes the Blob `href`s in a JSON value absolute: `blob/<id>` becomes
/// `{scheme}://{host}{prefix}/blob/<id>`, for Blobs that still exist.
void resolve_blobs(json::Value& value, const Urls& urls);

/// An invocation as JSON: its summary, and with `full` also its `input`,
/// `output`, `log` and `error`.
json::Value invocation_json(const InvocationRecord& record, const Urls& urls, bool full);
/// An invocation as the WoT Profile's `ActionStatus` (the `wot` profile).
json::Value action_status(const InvocationRecord& record, const Urls& urls, bool has_output);

/// A JSON answer.
Response json_response(int status, const json::Value& value);
/// A 204 with no body.
Response no_content();
/// A 307 to an absolute URL.
Response redirect(const std::string& location);
/// A 201 for a new invocation, with its URL in `location`.
Response created(const InvocationRecord& record, const json::Value& body, const Urls& urls);

/// What went wrong, before it is written in a profile's format.
struct HttpError {
    enum class What {
        /// An error with only an HTTP meaning.
        status,
        /// Invalid input.
        invalid,
        /// A problem reported by the runtime.
        problem,
        /// One of the framework's problem types.
        typed,
    };
    What what = What::status;
    int status = 500;
    std::string message;
    std::vector<ValidationIssue> issues;
    ProblemDetails problem;
    /// For `typed`: the status in the `wot` profile, and the type's name and
    /// title.
    int wot_status = 500;
    std::string name;
    std::string title;

    static HttpError with_status(int status, std::string message);
    static HttpError not_found() { return with_status(404, "Not Found"); }
    static HttpError invalid(std::vector<ValidationIssue> issues);
    static HttpError from_problem(ProblemDetails problem);
    static HttpError typed(int tetathing_status, int wot_status, std::string name, std::string title,
                           std::string detail);

    /// The answer, in the profile's format.
    Response response(WireProfile profile) const;
};

/// An error with only an HTTP meaning, as a problem (`about:blank`).
Response http_problem(int status, const std::string& message);
/// A runtime problem in the `wot` profile: its status and body.
std::pair<int, json::Value> wot_problem(const ProblemDetails& problem);
/// One `invalid-params` entry of the `wot` profile's validation problem.
json::Value invalid_param(const ValidationIssue& issue);

/// The answer to a CORS preflight.
Response preflight(std::string_view origin, std::optional<std::string_view> requested_headers);
/// Adds `vary: Origin`, and the mirrored origin with credentials if there is
/// one, to an answer.
void add_cors(Response& response, const std::optional<std::string>& origin);

}  // namespace teta::wot::http::detail
