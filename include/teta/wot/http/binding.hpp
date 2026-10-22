// The HTTP binding: a port of teta-wot-http (ADR-0004).
//
// `HttpBinding` answers requests for a runtime's Things, without sockets: a
// transport (the Drogon adapter, or `TestClient`) hands it each request and
// sends what it answers. It owns routing (with Starlette's trailing-slash
// redirects), the 404 and 405 answers, CORS, request bodies and every error
// body, so that answers are teta-wot's byte for byte.
//
// | Route | Methods |
// |---|---|
// | `{prefix}/{thing}/` | `GET` the Thing Description |
// | `{prefix}/{thing}/{property}` | `GET`, `PUT` if writable (201 and `null`; 204 in `wot`); `GET` with
// `Accept: text/event-stream` observes a data property | | `{prefix}/{thing}/{property}/reset` | `POST` if
// resettable | | `{prefix}/{thing}/{action}` | `POST` invokes (201 with `location`), `GET` lists its
// invocations | | `{prefix}/{thing}/properties` | `GET` reads them all (or observes them with server-sent
// events); `PUT` writes several (204) | | `{prefix}/{thing}/actions` | `GET` every action's invocations,
// newest first | | `{prefix}/{thing}/{event}`, `{prefix}/{thing}/events` | `GET` server-sent events | |
// `{prefix}/{thing}/{path}` | a custom endpoint | | `{prefix}/action_invocations[/{id}[/output]]` | `GET`;
// `DELETE` cancels | | `{prefix}/things/`, `{prefix}/thing_descriptions/` | `GET` |
//
// Thing code (getters, setters, custom endpoints, waiting for a synchronous
// action) runs on the runtime's worker pool, never on the caller's thread:
// a transport's I/O threads only parse, route and write.

#pragma once

#include <array>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "teta/wot/http/message.hpp"
#include "teta/wot/json.hpp"
#include "teta/wot/runtime.hpp"

namespace teta::wot::http {

/// The W3C WoT HTTP Basic Profile, in the `profile` of TDs in the `wot`
/// profile.
inline constexpr std::string_view http_basic_profile = "https://www.w3.org/2022/wot/profile/http-basic/v1";
/// The W3C WoT HTTP SSE Profile.
inline constexpr std::string_view http_sse_profile = "https://www.w3.org/2022/wot/profile/http-sse/v1";

/// Where the `wot` profile's problem types are documented.
inline constexpr std::string_view problem_types_url =
    "https://github.com/lucaswewa/wot/blob/main/docs/problems.md";
/// The `type` of one of the framework's problems, such as `validation-error`.
std::string problem_type(std::string_view name);

/// Thing names that would clash with the server's own routes.
inline constexpr std::array<std::string_view, 7> reserved_thing_names = {
    "things", "thing_descriptions", "action_invocations", "blob", "docs", "redoc", "directory"};

/// How the server behaves where teta-wot and the W3C differ. Server-wide.
enum class WireProfile {
    /// teta-wot's behaviour, which its clients depend on (the default).
    tetathing,
    /// The W3C WoT HTTP Basic and SSE Profiles: 204 for writes, 400 and
    /// `application/problem+json` for errors, `ActionStatus` objects,
    /// synchronous actions, named server-sent events.
    wot,
};

/// The profile's name in configuration files: `tetathing` or `wot`.
std::string_view to_string(WireProfile profile);
/// The profile named `name`, if there is one.
std::optional<WireProfile> wire_profile_from_name(std::string_view name);

/// The default server ID: the computer's name (`COMPUTERNAME`, or
/// `HOSTNAME`), or `localhost`.
std::string default_server_id();

/// Options of the HTTP binding.
struct HttpOptions {
    /// A prefix for every route, such as `/api/v1`, or empty.
    std::string api_prefix;
    /// Identifies the server in TD `id`s.
    std::string server_id = default_server_id();
    /// The API's title and version, in the OpenAPI document and the docs.
    std::string api_title = "teta-wot";
    std::string api_version = "0.1.0";
    WireProfile profile = WireProfile::tetathing;
};

/// The binding can't be set up.
class RouteError : public std::exception {
  public:
    enum class Kind {
        /// The API prefix isn't empty or like `/api/v1`.
        invalid_prefix,
        /// A Thing's name is one of the server's routes.
        reserved_thing_name,
        /// A custom endpoint answers a route that another already answers.
        endpoint_conflict,
    };
    RouteError(Kind kind, std::string subject, std::string message)
        : kind_(kind), subject_(std::move(subject)), message_(std::move(message)) {}

    Kind kind() const { return kind_; }
    /// The prefix, the Thing name, or the endpoint's method and path.
    const std::string& subject() const { return subject_; }
    const char* what() const noexcept override { return message_.c_str(); }

  private:
    Kind kind_;
    std::string subject_;
    std::string message_;
};

/// The HTTP binding of a runtime's Things (teta-wot-http's `router`). The
/// runtime is started and stopped separately (by `ThingServer`).
class HttpBinding {
  public:
    /// Receives the answer to a request.
    using Respond = std::function<void(Response)>;

    /// Builds the routes; throws RouteError.
    HttpBinding(std::shared_ptr<Runtime> runtime, HttpOptions options);
    HttpBinding(const HttpBinding&) = delete;
    HttpBinding& operator=(const HttpBinding&) = delete;
    ~HttpBinding();

    /// Answers a request. `respond` is called once: on this thread, or on a
    /// worker thread when the answer runs Thing code. The answer to a `HEAD`
    /// carries the body a `GET` would, which a transport doesn't send.
    void handle(Request request, Respond respond) const;

    const std::shared_ptr<Runtime>& runtime() const;
    const HttpOptions& options() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};

/// A Thing's Thing Description as a server at `base` (such as
/// `http://localhost:5000/`) serves it: with that base, the TD `id`, and the
/// forms of observation, events and invocations. Throws std::invalid_argument
/// for a Thing the runtime doesn't have.
json::Value thing_description(const Runtime& runtime, std::string_view thing, std::string_view base,
                              const HttpOptions& options);

}  // namespace teta::wot::http
