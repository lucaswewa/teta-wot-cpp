// The dispatcher: a port of teta-wot-http's `lib.rs` and `handlers.rs`.

#include "teta/wot/http/binding.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <variant>

#include "output.hpp"
#include "path_uuid.hpp"
#include "render.hpp"
#include "routes.hpp"
#include "sse.hpp"
#include "teta/wot/log.hpp"
#include "teta/wot/td/describe.hpp"
#include "websocket.hpp"

namespace teta::wot::http {

using detail::HttpError;
using detail::Kind;
using detail::Urls;

namespace {

/// The largest request body read (64 MiB), as teta-wot's.
constexpr std::size_t body_limit = 64 * 1024 * 1024;

std::optional<std::string> environment(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string out(value);
    std::free(value);
    return out;
#else
    if (const char* value = std::getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

}  // namespace

std::string problem_type(std::string_view name) {
    return std::string(problem_types_url) + "#" + std::string(name);
}

std::string_view to_string(WireProfile profile) {
    return profile == WireProfile::wot ? "wot" : "tetathing";
}

std::optional<WireProfile> wire_profile_from_name(std::string_view name) {
    if (name == "tetathing") {
        return WireProfile::tetathing;
    }
    if (name == "wot") {
        return WireProfile::wot;
    }
    return std::nullopt;
}

std::string default_server_id() {
    if (auto name = environment("COMPUTERNAME")) {
        return *name;
    }
    if (auto name = environment("HOSTNAME")) {
        return *name;
    }
    return "localhost";
}

namespace {

json::Value served_td(const Runtime& runtime, std::string_view thing, const Urls& urls,
                      const HttpOptions& options) {
    const auto* handle = runtime.thing(thing);
    if (!handle) {
        throw std::invalid_argument("no Thing named " + std::string(thing));
    }
    const bool wot = options.profile == WireProfile::wot;
    td::TdOptions td_options;
    td_options.path = urls.thing_path(thing);
    td_options.base = urls.base();
    td_options.id = td::thing_id(options.server_id, thing);
    td_options.observation = true;
    td_options.websocket = urls.websocket(thing);
    td_options.links = true;
    td_options.top_level = true;
    td_options.invocations = urls.prefix + "/action_invocations/";
    td_options.synchronous_actions = wot;
    if (wot) {
        td_options.profiles = {std::string(http_basic_profile), std::string(http_sse_profile)};
    }
    return td::to_json(handle->thing_description(td_options));
}

}  // namespace

json::Value thing_description(const Runtime& runtime, std::string_view thing, std::string_view base,
                              const HttpOptions& options) {
    return served_td(runtime, thing, Urls::from_base(base, options.api_prefix), options);
}

// ---- The binding ------------------------------------------------------------------------

struct HttpBinding::State {
    std::shared_ptr<Runtime> runtime;
    HttpOptions options;
    detail::Routes routes;
    std::shared_ptr<detail::SseStreams> streams = std::make_shared<detail::SseStreams>();

    bool wot() const { return options.profile == WireProfile::wot; }
    Response error(const HttpError& error) const { return error.response(options.profile); }

    // The answer to a successful write, reset or cancellation: nothing in
    // `wot`; teta-wot's status and `null` otherwise.
    Response written(int tetathing_status) const {
        return wot() ? detail::no_content() : detail::json_response(tetathing_status, nullptr);
    }

    // An invocation as the profile shows it.
    json::Value invocation(const InvocationRecord& record, const Urls& urls, bool full) const {
        if (wot()) {
            const auto* thing = runtime->thing(record.thing);
            const auto* action = thing ? thing->action(record.action) : nullptr;
            return detail::action_status(record, urls, !action || action->has_output());
        }
        return detail::invocation_json(record, urls, full);
    }

    // A property failure: invalid values are the profile's validation error;
    // the rest are problems (409 for a busy global lock, 500 otherwise).
    Response property_error(const std::string& thing, const PropertyError& failure) const {
        if (const auto* invalid = failure.validation()) {
            return error(HttpError::invalid(invalid->issues()));
        }
        log::error("{} (thing={})", failure.what(), thing);
        return error(HttpError::from_problem(failure.problem()));
    }

    // Reads a JSON body: empty is `null` (FastAPI treats both as missing);
    // malformed is FastAPI's `json_invalid`, at the character where parsing
    // failed (in `wot`, at the body, with the parser's message).
    std::variant<json::Value, Response> read_json(const std::string& body) const {
        if (body.size() > body_limit) {
            return error(HttpError::with_status(413, "length limit exceeded"));
        }
        if (body.empty()) {
            return json::Value(nullptr);
        }
        try {
            return json::parse(body);
        } catch (const json::ParseError& failure) {
            const auto position = json::character_offset(body, failure.line(), failure.column());
            ValidationIssue issue;
            issue.type = "json_invalid";
            if (wot()) {
                issue.loc = {"body"};
                issue.msg =
                    "JSON decode error at character " + std::to_string(position) + ": " + failure.message();
                issue.input = nullptr;
            } else {
                issue.loc = {"body", position};
                issue.msg = "JSON decode error";
                issue.input = json::Value::object();
                issue.ctx = json::Value{{"error", failure.message()}};
            }
            return error(HttpError::invalid({std::move(issue)}));
        }
    }

    // The ID in the path (the parameter `name`), or FastAPI's 422; in `wot`,
    // a malformed ID names nothing, so it is a 404.
    std::variant<Uuid, Response> parse_id(const std::optional<std::string>& param, const char* name) const {
        const std::string raw = param.value_or("");
        auto parsed = detail::parse_path_uuid(raw);
        if (const auto* id = std::get_if<Uuid>(&parsed)) {
            return *id;
        }
        if (wot()) {
            return error(HttpError::with_status(404, std::string_view(name) == "blob_id"
                                                         ? "Blob not found"
                                                         : "No action invocation found with ID " + raw));
        }
        const std::string& reason = std::get<std::string>(parsed);
        ValidationIssue issue;
        issue.type = "uuid_parsing";
        issue.loc = {"path", name};
        issue.msg = "Input should be a valid UUID, " + reason;
        issue.input = raw;
        issue.ctx = json::Value{{"error", reason}};
        return error(HttpError::invalid({std::move(issue)}));
    }

    std::variant<json::Value, Response> td(std::string_view thing, const Urls& urls) const {
        try {
            return served_td(*runtime, thing, urls, options);
        } catch (const std::exception& failure) {
            return error(HttpError::with_status(500, failure.what()));
        }
    }

    // `writemultipleproperties`: every value is checked before any is
    // written, so an invalid request changes nothing.
    Response write_multiple(const std::string& thing, const json::Value& body) const {
        const auto* handle = runtime->thing(thing);
        if (!handle) {
            return error(HttpError::not_found());
        }
        if (!body.is_object()) {
            return error(HttpError::invalid(std::vector<ValidationIssue>{ValidationIssue{
                "dict_type", {"body"}, "Input should be a valid dictionary", body, std::nullopt}}));
        }
        std::vector<ValidationIssue> issues;
        for (const auto& [name, value] : body.items()) {
            auto issue = [&](const char* type, const char* msg) {
                return ValidationIssue{type, {"body", name}, msg, value, std::nullopt};
            };
            const auto* property = handle->property(name);
            if (!property) {
                issues.push_back(issue("extra_forbidden", "Extra inputs are not permitted"));
                continue;
            }
            try {
                property->validate(value);
            } catch (const PropertyError& failure) {
                if (failure.kind() == PropertyError::Kind::read_only) {
                    issues.push_back(issue("frozen_field", "Field is frozen"));
                } else if (const auto* invalid = failure.validation()) {
                    for (auto found : invalid->issues()) {
                        found.loc.insert(found.loc.begin() + 1, LocItem(name));
                        issues.push_back(std::move(found));
                    }
                } else {
                    return property_error(thing, failure);
                }
            }
        }
        if (!issues.empty()) {
            return error(HttpError::invalid(std::move(issues)));
        }
        for (const auto& [name, value] : body.items()) {
            if (const auto* property = handle->property(name)) {
                try {
                    property->write(value);
                } catch (const PropertyError& failure) {
                    return property_error(thing, failure);
                }
            }
        }
        return detail::no_content();
    }

    // Every endpoint but streams and custom endpoints.
    Response answer(const detail::Endpoint& endpoint, const std::optional<std::string>& param,
                    const Request& request, const Urls& urls) const {
        auto& invocations = runtime->invocations();
        switch (endpoint.kind) {
            case Kind::thing_description: {
                auto described = td(endpoint.thing, urls);
                if (auto* failed = std::get_if<Response>(&described)) {
                    return std::move(*failed);
                }
                return detail::json_response(200, std::get<json::Value>(described));
            }
            case Kind::thing_descriptions: {
                json::Value out = json::Value::object();
                for (const auto& thing : runtime->things()) {
                    auto described = td(thing->name(), urls);
                    if (auto* failed = std::get_if<Response>(&described)) {
                        return std::move(*failed);
                    }
                    out[thing->name()] = std::move(std::get<json::Value>(described));
                }
                return detail::json_response(200, out);
            }
            case Kind::thing_paths: {
                json::Value out = json::Value::object();
                for (const auto& thing : runtime->things()) {
                    out[thing->name()] = urls.absolute(urls.thing_path(thing->name()));
                }
                return detail::json_response(200, out);
            }
            case Kind::read_property: {
                const auto* entry = property(endpoint);
                if (!entry) {
                    return error(HttpError::not_found());
                }
                try {
                    auto value = entry->read();
                    detail::resolve_blobs(value, urls);
                    return detail::json_response(200, value);
                } catch (const PropertyError& failure) {
                    return property_error(endpoint.thing, failure);
                }
            }
            case Kind::write_property: {
                const auto* entry = property(endpoint);
                if (!entry) {
                    return error(HttpError::not_found());
                }
                auto value = read_json(request.body);
                if (auto* refused = std::get_if<Response>(&value)) {
                    return std::move(*refused);
                }
                try {
                    entry->write(std::get<json::Value>(value));
                    return written(201);
                } catch (const PropertyError& failure) {
                    return property_error(endpoint.thing, failure);
                }
            }
            case Kind::reset_property: {
                const auto* entry = property(endpoint);
                if (!entry) {
                    return error(HttpError::not_found());
                }
                try {
                    entry->reset();
                    return written(200);
                } catch (const PropertyError& failure) {
                    return property_error(endpoint.thing, failure);
                }
            }
            case Kind::read_all_properties: {
                const auto* handle = runtime->thing(endpoint.thing);
                if (!handle) {
                    return error(HttpError::not_found());
                }
                std::vector<const PropertyEntry*> properties;
                for (const auto& p : handle->properties()) {
                    properties.push_back(&p);
                }
                std::ranges::sort(properties, {},
                                  [](const PropertyEntry* p) -> const std::string& { return p->name(); });
                json::Value values = json::Value::object();
                for (const auto* p : properties) {
                    try {
                        auto value = p->read();
                        detail::resolve_blobs(value, urls);
                        values[p->name()] = std::move(value);
                    } catch (const PropertyError& failure) {
                        return property_error(endpoint.thing, failure);
                    }
                }
                return detail::json_response(200, values);
            }
            case Kind::write_multiple_properties: {
                auto value = read_json(request.body);
                if (auto* refused = std::get_if<Response>(&value)) {
                    return std::move(*refused);
                }
                return write_multiple(endpoint.thing, std::get<json::Value>(value));
            }
            case Kind::invoke_action:
                return invoke(endpoint, request, urls);
            case Kind::list_action_invocations: {
                json::Value list = json::Value::array();
                for (const auto& i : invocations.list_for(endpoint.thing, endpoint.affordance)) {
                    list.push_back(invocation(i->record(), urls, false));
                }
                return detail::json_response(200, list);
            }
            case Kind::query_all_actions: {
                const auto* handle = runtime->thing(endpoint.thing);
                if (!handle) {
                    return error(HttpError::not_found());
                }
                json::Value out = json::Value::object();
                for (const auto& action : handle->actions()) {
                    std::vector<InvocationRecord> records;
                    for (const auto& i : invocations.list_for(endpoint.thing, action.name())) {
                        records.push_back(i->record());
                    }
                    // The most recent first, as the WoT Profile wants.
                    std::ranges::stable_sort(records, std::ranges::greater{},
                                             [](const InvocationRecord& r) { return r.time_requested; });
                    json::Value list = json::Value::array();
                    for (const auto& record : records) {
                        list.push_back(invocation(record, urls, false));
                    }
                    out[action.name()] = std::move(list);
                }
                return detail::json_response(200, out);
            }
            case Kind::list_invocations: {
                json::Value list = json::Value::array();
                for (const auto& i : invocations.list()) {
                    list.push_back(invocation(i->record(), urls, false));
                }
                return detail::json_response(200, list);
            }
            case Kind::get_invocation: {
                auto id = parse_id(param, "id");
                if (auto* refused = std::get_if<Response>(&id)) {
                    return std::move(*refused);
                }
                const auto& uuid = std::get<Uuid>(id);
                if (const auto found = invocations.get(uuid)) {
                    return detail::json_response(200, invocation(found->record(), urls, true));
                }
                return error(
                    HttpError::with_status(404, "No action invocation found with ID " + uuid.to_string()));
            }
            case Kind::cancel_invocation: {
                auto id = parse_id(param, "id");
                if (auto* refused = std::get_if<Response>(&id)) {
                    return std::move(*refused);
                }
                try {
                    invocations.cancel(std::get<Uuid>(id));
                    return written(200);
                } catch (const CancelError& failure) {
                    if (failure.kind() == CancelError::Kind::not_found) {
                        return error(HttpError::with_status(404, failure.what()));
                    }
                    return error(
                        HttpError::typed(503, 409, "not-cancellable", "Not cancellable", failure.what()));
                }
            }
            case Kind::invocation_output: {
                auto id = parse_id(param, "id");
                if (auto* refused = std::get_if<Response>(&id)) {
                    return std::move(*refused);
                }
                const auto& uuid = std::get<Uuid>(id);
                const auto found = invocations.get(uuid);
                if (!found) {
                    return error(HttpError::with_status(
                        404, "No action invocation found with ID " + uuid.to_string()));
                }
                // A Blob output is its data.
                if (const auto blob = found->output_blob()) {
                    return detail::blob_response(blob);
                }
                // Falsy outputs such as 0 are returned; only a missing or
                // null output is "no result" (reference bug B3 fixed).
                if (auto output = found->output(); output && !output->is_null()) {
                    detail::resolve_blobs(*output, urls);
                    return detail::json_response(200, *output);
                }
                return error(HttpError::typed(503, 404, "no-output", "No output",
                                              "No result is available for this invocation"));
            }
            case Kind::download_blob: {
                auto id = parse_id(param, "blob_id");
                if (auto* refused = std::get_if<Response>(&id)) {
                    return std::move(*refused);
                }
                if (const auto data = blob::BlobData::find(std::get<Uuid>(id))) {
                    return detail::blob_response(data);
                }
                return error(HttpError::with_status(404, "Blob not found"));
            }
            case Kind::subscribe_event:
            case Kind::subscribe_all_events:
            case Kind::stream:
            case Kind::stream_viewer:
            case Kind::custom:
                break;  // answered by `route`
        }
        return detail::http_problem(500, "Internal Server Error");
    }

    Response invoke(const detail::Endpoint& endpoint, const Request& request, const Urls& urls) const {
        const auto* thing = runtime->thing(endpoint.thing);
        const auto* entry = thing ? thing->action(endpoint.affordance) : nullptr;
        if (!entry) {
            return error(HttpError::not_found());
        }
        auto input = read_json(request.body);
        if (auto* refused = std::get_if<Response>(&input)) {
            return std::move(*refused);
        }
        std::shared_ptr<Invocation> started;
        try {
            started = entry->invoke(std::get<json::Value>(input));
        } catch (const ValidationError& invalid) {
            return error(HttpError::invalid(invalid.issues()));
        }
        if (wot() && entry->is_synchronous()) {
            // A synchronous action's caller waits for its output. If the
            // caller goes away, the action still finishes.
            started->wait();
            const auto record = started->record();
            if (record.status == InvocationStatus::completed) {
                if (!entry->has_output()) {
                    return detail::no_content();
                }
                auto output = record.output.value_or(nullptr);
                detail::resolve_blobs(output, urls);
                return detail::json_response(200, output);
            }
            return error(HttpError::from_problem(record.error.value_or(ProblemDetails::teta_wot_things(
                "InvocationCancelledError", "The action was cancelled.", 500))));
        }
        const auto record = started->record();
        return detail::created(record, invocation(record, urls, true), urls);
    }

    const PropertyEntry* property(const detail::Endpoint& endpoint) const {
        const auto* thing = runtime->thing(endpoint.thing);
        return thing ? thing->property(endpoint.affordance) : nullptr;
    }

    // A server-sent events answer for several affordances of a Thing: its
    // observable properties, or its events. Always named.
    Response all_stream(const std::string& thing, bool events, const Urls& urls) const {
        const auto* handle = runtime->thing(thing);
        if (!handle) {
            return error(HttpError::not_found());
        }
        const auto& broker = runtime->broker();
        auto subscription = broker->subscription();
        if (events) {
            for (const auto& event : handle->events()) {
                broker->add(subscription, thing, event.name);
            }
        } else {
            for (const auto& property : handle->properties()) {
                if (property.is_observable()) {
                    broker->add(subscription, thing, property.name());
                }
            }
        }
        return streams->response(std::move(subscription), true, urls);
    }

    // Whether answering the endpoint may run Thing code, or read and parse a
    // body, and so belongs on a worker thread rather than the transport's.
    bool on_worker(const detail::Endpoint& endpoint) const {
        switch (endpoint.kind) {
            case Kind::read_property: {
                const auto* entry = property(endpoint);
                return entry && !entry->is_observable();  // a getter
            }
            case Kind::write_property:
            case Kind::reset_property:
            case Kind::read_all_properties:
            case Kind::write_multiple_properties:
            case Kind::invoke_action:
            case Kind::custom:
                return true;
            default:
                return false;
        }
    }

    static void route(const std::shared_ptr<const State>& state, const detail::Route& matched,
                      std::optional<std::string> param, Request request, Urls urls, Respond respond) {
        const auto& endpoint = matched.endpoint;
        const auto& broker = state->runtime->broker();
        switch (endpoint.kind) {
            case Kind::read_property:
                if (detail::wants_event_stream(request.headers)) {
                    if (const auto* entry = state->property(endpoint); entry && entry->is_observable()) {
                        respond(state->streams->response(
                            broker->subscribe(endpoint.thing, endpoint.affordance), state->wot(), urls));
                        return;
                    }
                }
                break;
            case Kind::subscribe_event:
                respond(state->streams->response(broker->subscribe(endpoint.thing, endpoint.affordance),
                                                 state->wot(), urls));
                return;
            case Kind::read_all_properties:
                if (detail::wants_event_stream(request.headers)) {
                    respond(state->all_stream(endpoint.thing, false, urls));
                    return;
                }
                break;
            case Kind::subscribe_all_events:
                respond(state->all_stream(endpoint.thing, true, urls));
                return;
            case Kind::stream: {
                const auto* thing = state->runtime->thing(endpoint.thing);
                const auto* stream = thing ? thing->stream(endpoint.affordance) : nullptr;
                respond(stream ? detail::mjpeg_response(*stream, broker)
                               : state->error(HttpError::not_found()));
                return;
            }
            case Kind::stream_viewer:
                respond(detail::viewer_page(urls.thing_path(endpoint.thing) + endpoint.affordance));
                return;
            default:
                break;
        }
        if (!state->on_worker(endpoint)) {
            respond(state->answer(endpoint, param, request, urls));
            return;
        }
        state->runtime->pool()->submit([state, &matched, param = std::move(param),
                                        request = std::move(request), urls = std::move(urls),
                                        respond = std::move(respond)]() mutable {
            Response response;
            try {
                if (matched.endpoint.kind == Kind::custom) {
                    response = state->routes.endpoints()[matched.endpoint.index](request);
                } else {
                    response = state->answer(matched.endpoint, param, request, urls);
                }
            } catch (const std::exception& failure) {
                log::error("{} {} failed: {}", request.method, request.path, failure.what());
                response = detail::http_problem(500, failure.what());
            } catch (...) {
                log::error("{} {} failed", request.method, request.path);
                response = detail::http_problem(500, "Internal Server Error");
            }
            respond(std::move(response));
        });
    }

    static void dispatch(const std::shared_ptr<const State>& state, Request request, Respond respond) {
        Urls urls = Urls::from_request(request, state->options.api_prefix);
        auto found = state->routes.find(request.method, request.path);
        switch (found.what) {
            case detail::Found::What::route:
                route(state, *found.route, std::move(found.param), std::move(request), std::move(urls),
                      std::move(respond));
                return;
            case detail::Found::What::wrong_method: {
                Response response = state->error(HttpError::with_status(405, "Method Not Allowed"));
                response.headers.set("allow", found.route->allow ? found.route->allow : found.route->method);
                respond(std::move(response));
                return;
            }
            case detail::Found::What::nothing:
                break;
        }
        // Starlette's redirect to the path with (or without) a trailing
        // slash, if that path is a route.
        const std::string& path = request.path;
        std::string toggled = path;
        if (path.ends_with('/')) {
            while (!toggled.empty() && toggled.back() == '/') {
                toggled.pop_back();
            }
        } else {
            toggled += '/';
        }
        if (path != "/" && state->routes.has_path(toggled)) {
            respond(detail::redirect(urls.absolute(toggled + (request.query ? "?" + *request.query : ""))));
            return;
        }
        respond(state->error(HttpError::not_found()));
    }
};

bool valid_api_prefix(std::string_view prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (prefix.front() != '/') {
        return false;
    }
    for (std::size_t start = 1;;) {
        const auto end = prefix.find('/', start);
        const auto segment =
            prefix.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (segment.empty() || !std::ranges::all_of(segment, [](char c) {
                const auto b = static_cast<unsigned char>(c);
                return b >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
            })) {
            return false;
        }
        if (end == std::string_view::npos) {
            return true;
        }
        start = end + 1;
    }
}

HttpBinding::HttpBinding(std::shared_ptr<Runtime> runtime, HttpOptions options) {
    if (!valid_api_prefix(options.api_prefix)) {
        throw RouteError(
            RouteError::Kind::invalid_prefix, options.api_prefix,
            "invalid API prefix `" + options.api_prefix + "`: it must be empty or like `/api/v1`");
    }
    auto routes = detail::Routes::build(*runtime, options.api_prefix);
    state_ = std::make_shared<State>(State{std::move(runtime), std::move(options), std::move(routes),
                                           std::make_shared<detail::SseStreams>()});
}

HttpBinding::~HttpBinding() = default;

void HttpBinding::handle(Request request, Respond respond) const {
    std::optional<std::string> origin;
    if (const auto value = request.headers.get("origin")) {
        origin = std::string(*value);
    }
    // A CORS preflight is answered on any path.
    if (origin && request.method == "OPTIONS" && request.headers.contains("access-control-request-method")) {
        respond(detail::preflight(*origin, request.headers.get("access-control-request-headers")));
        return;
    }
    State::dispatch(state_, std::move(request),
                    [respond = std::move(respond), origin = std::move(origin)](Response response) {
                        detail::add_cors(response, origin);
                        respond(std::move(response));
                    });
}

bool HttpBinding::is_websocket(const Request& request) const {
    return detail::websocket_thing(*state_->runtime, state_->options.api_prefix, request) != nullptr;
}

std::shared_ptr<WebSocketSession> HttpBinding::open_websocket(const Request& request,
                                                              std::shared_ptr<WebSocketPeer> peer) const {
    const auto* thing = detail::websocket_thing(*state_->runtime, state_->options.api_prefix, request);
    if (!thing) {
        throw std::invalid_argument("`" + request.path + "` isn't a Thing's WebSocket");
    }
    return detail::open_session(state_->runtime->broker(), thing->shared_from_this(), std::move(peer),
                                Urls::from_request(request, state_->options.api_prefix));
}

const std::shared_ptr<Runtime>& HttpBinding::runtime() const {
    return state_->runtime;
}

const HttpOptions& HttpBinding::options() const {
    return state_->options;
}

}  // namespace teta::wot::http
