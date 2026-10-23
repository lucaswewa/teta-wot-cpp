// The route table: a port of teta-wot-http's `routes.rs`. Routes are tried in
// the order they were added; the first whose path and method match answers,
// and a 405 names the method of the first whose path matches.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "teta/wot/http/message.hpp"
#include "teta/wot/runtime.hpp"

namespace teta::wot::http::detail {

/// What a route does.
enum class Kind {
    /// `/openapi.json`, and the docs pages.
    openapi,
    swagger_ui,
    oauth2_redirect,
    redoc,
    /// A compiled-in docs asset: `index` in `docs_assets()`.
    docs_asset,
    thing_description,
    read_property,
    write_property,
    reset_property,
    invoke_action,
    list_action_invocations,
    /// `readallproperties`, or `observeallproperties` with server-sent events.
    read_all_properties,
    write_multiple_properties,
    query_all_actions,
    subscribe_all_events,
    subscribe_event,
    list_invocations,
    get_invocation,
    cancel_invocation,
    invocation_output,
    download_blob,
    /// An MJPEG stream, and its viewer page.
    stream,
    stream_viewer,
    thing_paths,
    thing_descriptions,
    /// A custom endpoint: `index` in `Routes::endpoints`.
    custom,
};

struct Endpoint {
    Kind kind;
    std::string thing;
    /// The property, action, event or stream.
    std::string affordance;
    std::size_t index = 0;

    /// Whether the route describes rather than interacts (and so will need no
    /// credentials, from Phase 9): TDs and the docs.
    bool is_public() const;
};

struct Route {
    /// The path's segments; `nullopt` is the path parameter (any non-empty
    /// segment).
    std::vector<std::optional<std::string>> segments;
    std::string method;
    Endpoint endpoint;
    /// The `allow` of a 405, if not `method`.
    const char* allow = nullptr;

    /// Whether `path` matches, and its parameter if it has one.
    bool matches(std::string_view path, std::optional<std::string>* param = nullptr) const;
};

/// The result of looking a request up.
struct Found {
    enum class What {
        /// Path and method match.
        route,
        /// The path matches a route with another method.
        wrong_method,
        nothing,
    };
    What what = What::nothing;
    const Route* route = nullptr;
    std::optional<std::string> param;
};

class Routes {
  public:
    /// Every route for the runtime's Things; throws RouteError for a reserved
    /// Thing name or a custom endpoint whose route is taken.
    static Routes build(const Runtime& runtime, const std::string& prefix);

    Found find(std::string_view method, std::string_view path) const;
    /// Whether any route has this path, whatever its method.
    bool has_path(std::string_view path) const;
    /// The handlers of custom endpoints.
    const std::vector<EndpointHandler>& endpoints() const { return endpoints_; }

    /// Adds a route (for the route table's own tests).
    void add(const std::string& pattern, std::string method, Endpoint endpoint);
    /// Adds a `GET` and a `HEAD` route, whose 405s allow both (Starlette's
    /// plain routes).
    void add_get_and_head(const std::string& path, const Endpoint& endpoint);

  private:
    std::vector<Route> routes_;
    std::vector<EndpointHandler> endpoints_;
};

/// Whether `method` is a valid HTTP method (a token).
bool is_method_token(std::string_view method);

}  // namespace teta::wot::http::detail
