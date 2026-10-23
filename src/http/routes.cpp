#include "routes.hpp"

#include <algorithm>

#include "docs.hpp"
#include "teta/wot/http/binding.hpp"

namespace teta::wot::http::detail {

bool Endpoint::is_public() const {
    switch (kind) {
        case Kind::thing_description:
        case Kind::thing_descriptions:
        case Kind::thing_paths:
        case Kind::openapi:
        case Kind::swagger_ui:
        case Kind::oauth2_redirect:
        case Kind::redoc:
        case Kind::docs_asset:
            return true;
        default:
            return false;
    }
}

bool Route::matches(std::string_view path, std::optional<std::string>* param) const {
    std::size_t start = 0;
    std::optional<std::string> found;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto end = path.find('/', start);
        // Too few parts, or too many (the last segment must reach the end).
        if ((end == std::string_view::npos) != (i + 1 == segments.size())) {
            return false;
        }
        const auto part =
            path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (segments[i]) {
            if (part != *segments[i]) {
                return false;
            }
        } else if (part.empty()) {
            return false;
        } else {
            found = std::string(part);
        }
        start = end + 1;
    }
    if (param) {
        *param = std::move(found);
    }
    return true;
}

bool is_method_token(std::string_view method) {
    static constexpr std::string_view others = "!#$%&'*+-.^_`|~";
    return !method.empty() && std::ranges::all_of(method, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               others.find(c) != std::string_view::npos;
    });
}

void Routes::add(const std::string& pattern, std::string method, Endpoint endpoint) {
    Route route;
    for (std::size_t start = 0;;) {
        const auto end = pattern.find('/', start);
        const auto segment =
            pattern.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (segment == "{id}") {
            route.segments.emplace_back(std::nullopt);
        } else {
            route.segments.emplace_back(segment);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    route.method = std::move(method);
    route.endpoint = std::move(endpoint);
    routes_.push_back(std::move(route));
}

void Routes::add_get_and_head(const std::string& path, const Endpoint& endpoint) {
    for (const char* method : {"GET", "HEAD"}) {
        add(path, method, endpoint);
        routes_.back().allow = "GET, HEAD";
    }
}

Routes Routes::build(const Runtime& runtime, const std::string& prefix) {
    Routes routes;
    routes.add_get_and_head("/openapi.json", {Kind::openapi, {}, {}});
    routes.add_get_and_head("/docs", {Kind::swagger_ui, {}, {}});
    routes.add_get_and_head("/docs/oauth2-redirect", {Kind::oauth2_redirect, {}, {}});
    routes.add_get_and_head("/redoc", {Kind::redoc, {}, {}});
    const auto assets = docs_assets();
    for (std::size_t i = 0; i < assets.size(); ++i) {
        routes.add_get_and_head(std::string(assets[i].path), {Kind::docs_asset, {}, {}, i});
    }
    const std::string invocations = prefix + "/action_invocations";
    routes.add(invocations, "GET", {Kind::list_invocations, {}, {}});
    routes.add(invocations + "/{id}", "GET", {Kind::get_invocation, {}, {}});
    routes.add(invocations + "/{id}/output", "GET", {Kind::invocation_output, {}, {}});
    routes.add(invocations + "/{id}", "DELETE", {Kind::cancel_invocation, {}, {}});
    routes.add(prefix + "/blob/{id}", "GET", {Kind::download_blob, {}, {}});
    routes.add(prefix + "/thing_descriptions/", "GET", {Kind::thing_descriptions, {}, {}});
    routes.add(prefix + "/things/", "GET", {Kind::thing_paths, {}, {}});

    for (const auto& thing : runtime.things()) {
        const std::string& name = thing->name();
        if (std::ranges::find(reserved_thing_names, name) != reserved_thing_names.end()) {
            throw RouteError(RouteError::Kind::reserved_thing_name, name,
                             "the Thing name `" + name + "` is reserved for the server's own routes");
        }
        const std::string base = prefix + "/" + name + "/";
        for (const auto& property : thing->properties()) {
            const std::string path = base + property.name();
            if (!property.is_read_only()) {
                routes.add(path, "PUT", {Kind::write_property, name, property.name()});
            }
            routes.add(path, "GET", {Kind::read_property, name, property.name()});
            if (property.is_resettable()) {
                routes.add(path + "/reset", "POST", {Kind::reset_property, name, property.name()});
            }
        }
        for (const auto& action : thing->actions()) {
            const std::string path = base + action.name();
            routes.add(path, "POST", {Kind::invoke_action, name, action.name()});
            routes.add(path, "GET", {Kind::list_action_invocations, name, action.name()});
        }
        for (const auto& event : thing->events()) {
            routes.add(base + event.name, "GET", {Kind::subscribe_event, name, event.name});
        }
        for (const auto& [stream, _] : thing->streams()) {
            routes.add(base + stream, "GET", {Kind::stream, name, stream});
            routes.add(base + stream + "/viewer", "GET", {Kind::stream_viewer, name, stream});
        }
        // The top-level resources, where the TD has their forms.
        if (!thing->properties().empty()) {
            routes.add(base + "properties", "GET", {Kind::read_all_properties, name, {}});
        }
        if (std::ranges::any_of(thing->properties(), [](const auto& p) { return !p.is_read_only(); })) {
            routes.add(base + "properties", "PUT", {Kind::write_multiple_properties, name, {}});
        }
        if (!thing->actions().empty()) {
            routes.add(base + "actions", "GET", {Kind::query_all_actions, name, {}});
        }
        if (!thing->events().empty()) {
            routes.add(base + "events", "GET", {Kind::subscribe_all_events, name, {}});
        }
        for (const auto& endpoint : thing->endpoints()) {
            const std::string path = base + endpoint.path;
            const bool taken = std::ranges::any_of(routes.routes_, [&](const Route& r) {
                return r.method == endpoint.method && r.matches(path);
            });
            if (taken || !is_method_token(endpoint.method)) {
                const std::string route = endpoint.method + " " + path;
                throw RouteError(
                    RouteError::Kind::endpoint_conflict, route,
                    "an endpoint of Thing `" + name + "` answers `" + route + "`, which is already taken");
            }
            routes.endpoints_.push_back(endpoint.handler);
            routes.add(path, endpoint.method, {Kind::custom, name, {}, routes.endpoints_.size() - 1});
        }
        routes.add(base, "GET", {Kind::thing_description, name, {}});
    }
    return routes;
}

Found Routes::find(std::string_view method, std::string_view path) const {
    const Route* wrong_method = nullptr;
    for (const auto& route : routes_) {
        std::optional<std::string> param;
        if (route.matches(path, &param)) {
            if (route.method == method) {
                return {Found::What::route, &route, std::move(param)};
            }
            if (!wrong_method) {
                wrong_method = &route;
            }
        }
    }
    if (wrong_method) {
        return {Found::What::wrong_method, wrong_method, std::nullopt};
    }
    return {};
}

bool Routes::has_path(std::string_view path) const {
    return std::ranges::any_of(routes_, [&](const Route& r) { return r.matches(path); });
}

}  // namespace teta::wot::http::detail
