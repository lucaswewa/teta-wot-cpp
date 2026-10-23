#include "render.hpp"

#include <ctime>
#include <format>
#include <utility>

#include "teta/wot/log.hpp"

namespace teta::wot::http::detail {

namespace {

constexpr std::string_view problem_json = "application/problem+json";
constexpr std::string_view all_methods = "DELETE, GET, HEAD, OPTIONS, PATCH, POST, PUT, QUERY";
constexpr std::string_view preflight_vary =
    "Origin, Access-Control-Request-Method, Access-Control-Request-Headers, "
    "Access-Control-Request-Private-Network";

// What `HeaderValue::to_str` accepts: visible ASCII, spaces and tabs.
bool is_header_text(std::string_view value) {
    for (const char c : value) {
        const auto b = static_cast<unsigned char>(c);
        if ((b < 0x20 && b != '\t') || b > 0x7E) {
            return false;
        }
    }
    return true;
}

// Seconds since the epoch, and the nanoseconds into the second.
std::pair<std::time_t, std::int64_t> split(std::chrono::system_clock::time_point time) {
    using namespace std::chrono;
    const auto nanos = duration_cast<nanoseconds>(time.time_since_epoch()).count();
    auto seconds = nanos / 1'000'000'000;
    auto fraction = nanos % 1'000'000'000;
    if (fraction < 0) {
        fraction += 1'000'000'000;
        seconds -= 1;
    }
    return {static_cast<std::time_t>(seconds), fraction};
}

std::string format_tm(const std::tm& tm) {
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec);
}

json::Value optional_time(const std::optional<std::chrono::system_clock::time_point>& time) {
    if (time) {
        return naive_local(*time);
    }
    return nullptr;
}

// A problem response: `application/problem+json`.
Response problem_response(int status, const json::Value& body) {
    Response response = json_response(status, body);
    response.headers.set("content-type", std::string(problem_json));
    return response;
}

// 400, with one `invalid-params` entry per issue.
Response validation_problem(const std::vector<ValidationIssue>& issues) {
    json::Value params = json::Value::array();
    for (const auto& issue : issues) {
        params.push_back(invalid_param(issue));
    }
    const auto count = issues.size();
    return problem_response(400,
                            {{"type", problem_type("validation-error")},
                             {"title", "Validation error"},
                             {"status", 400},
                             {"detail", std::format("{} validation error{}", count, count == 1 ? "" : "s")},
                             {"invalid-params", std::move(params)}});
}

// The problem of an invocation that ended without one (it was cancelled).
ProblemDetails cancelled_problem() {
    return ProblemDetails::teta_wot_things("InvocationCancelledError", "The action was cancelled.", 500);
}

}  // namespace

// ---- URLs -------------------------------------------------------------------------------

Urls Urls::from_request(const Request& request, const std::string& prefix) {
    const std::string scheme = request.scheme.value_or("http");
    std::string host = "localhost";
    if (const auto header = request.headers.get("host"); header && is_header_text(*header)) {
        host = std::string(*header);
    } else if (request.authority) {
        host = *request.authority;
    }
    return {scheme + "://" + host, prefix};
}

Urls Urls::from_base(std::string_view base, const std::string& prefix) {
    while (!base.empty() && base.back() == '/') {
        base.remove_suffix(1);
    }
    return {std::string(base), prefix};
}

std::string Urls::websocket(std::string_view thing) const {
    std::string ws = origin;
    if (const auto separator = origin.find("://"); separator != std::string::npos) {
        const std::string host = origin.substr(separator + 3);
        ws = (origin.substr(0, separator) == "https" ? "wss://" : "ws://") + host;
    }
    return ws + thing_path(thing) + "ws";
}

std::string Urls::invocation_href(const Uuid& id) const {
    return absolute(prefix + "/action_invocations/" + id.to_string());
}

// ---- Times ------------------------------------------------------------------------------

std::string naive_local(std::chrono::system_clock::time_point time) {
    const auto [seconds, nanos] = split(time);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &seconds);
#else
    localtime_r(&seconds, &tm);
#endif
    const auto micros = nanos / 1000;
    if (micros == 0) {
        return format_tm(tm);
    }
    return format_tm(tm) + std::format(".{:06}", micros);
}

std::string rfc3339(std::chrono::system_clock::time_point time, int digits) {
    const auto [seconds, nanos] = split(time);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    if (digits == 9) {
        return format_tm(tm) + std::format(".{:09}Z", nanos);
    }
    return format_tm(tm) + std::format(".{:06}Z", nanos / 1000);
}

// ---- Invocations --------------------------------------------------------------------------

void resolve_blobs(json::Value& value, const Urls& urls) {
    if (value.is_object()) {
        if (const auto href = value.find("href"); href != value.end() && href->is_string()) {
            if (const auto id = blob::local_href_id(href->get<std::string>())) {
                *href = urls.absolute(urls.prefix + "/blob/" + id->to_string());
            }
        }
        for (auto& [key, item] : value.items()) {
            resolve_blobs(item, urls);
        }
    } else if (value.is_array()) {
        for (auto& item : value) {
            resolve_blobs(item, urls);
        }
    }
}

json::Value invocation_json(const InvocationRecord& record, const Urls& urls, bool full) {
    const std::string href = urls.invocation_href(record.id);
    auto link = [](std::string target, const char* rel) {
        return json::Value{{"href", std::move(target)}, {"type", nullptr}, {"rel", rel}, {"anchor", nullptr}};
    };
    json::Value out = json::Value::object();
    out["status"] = std::string(to_string(record.status));
    out["id"] = record.id.to_string();
    out["action"] = urls.thing_path(record.thing) + record.action;
    out["href"] = href;
    out["timeStarted"] = optional_time(record.time_started);
    out["timeRequested"] = naive_local(record.time_requested);
    out["timeCompleted"] = optional_time(record.time_completed);
    out["links"] = json::Value::array({link(href, "self"), link(href + "/output", "output")});
    if (full) {
        out["input"] = record.input;
        out["output"] = record.output ? *record.output : json::Value(nullptr);
        resolve_blobs(out["input"], urls);
        resolve_blobs(out["output"], urls);
        json::Value log = json::Value::array();
        for (const auto& entry : record.log) {
            log.push_back(to_json(entry));
        }
        out["log"] = std::move(log);
        out["error"] = record.error ? to_json(*record.error) : json::Value(nullptr);
    }
    return out;
}

json::Value action_status(const InvocationRecord& record, const Urls& urls, bool has_output) {
    json::Value out = json::Value::object();
    const char* status = "failed";
    switch (record.status) {
        case InvocationStatus::pending:
            status = "pending";
            break;
        case InvocationStatus::running:
            status = "running";
            break;
        case InvocationStatus::completed:
            status = "completed";
            break;
        case InvocationStatus::error:
        case InvocationStatus::cancelled:
            break;
    }
    out["status"] = status;
    if (record.status == InvocationStatus::completed && has_output) {
        out["output"] = record.output ? *record.output : json::Value(nullptr);
        resolve_blobs(out["output"], urls);
    }
    if (std::string_view(status) == "failed") {
        out["error"] = wot_problem(record.error.value_or(cancelled_problem())).second;
    }
    out["href"] = urls.invocation_href(record.id);
    out["timeRequested"] = rfc3339(record.time_requested, 6);
    if (record.time_completed) {
        out["timeEnded"] = rfc3339(*record.time_completed, 6);
    }
    return out;
}

// ---- Answers ------------------------------------------------------------------------------

Response json_response(int status, const json::Value& value) {
    return Response::json(status, value);
}

Response no_content() {
    return Response::empty(204);
}

Response redirect(const std::string& location) {
    Response response = Response::empty(307);
    response.headers.set("location", location);
    return response;
}

Response created(const InvocationRecord& record, const json::Value& body, const Urls& urls) {
    Response response = json_response(201, body);
    response.headers.set("location", urls.invocation_href(record.id));
    return response;
}

// ---- Errors -------------------------------------------------------------------------------

HttpError HttpError::with_status(int status, std::string message) {
    HttpError error;
    error.what = What::status;
    error.status = status;
    error.message = std::move(message);
    return error;
}

HttpError HttpError::invalid(std::vector<ValidationIssue> issues) {
    HttpError error;
    error.what = What::invalid;
    error.issues = std::move(issues);
    return error;
}

HttpError HttpError::from_problem(ProblemDetails problem) {
    HttpError error;
    error.what = What::problem;
    error.problem = std::move(problem);
    return error;
}

HttpError HttpError::typed(int tetathing_status, int wot_status, std::string name, std::string title,
                           std::string detail) {
    HttpError error;
    error.what = What::typed;
    error.status = tetathing_status;
    error.wot_status = wot_status;
    error.name = std::move(name);
    error.title = std::move(title);
    error.message = std::move(detail);
    return error;
}

Response HttpError::response(WireProfile profile) const {
    if (profile == WireProfile::tetathing) {
        switch (what) {
            case What::status:
            case What::typed:
                return json_response(status, {{"detail", message}});
            case What::invalid: {
                json::Value list = json::Value::array();
                for (const auto& issue : issues) {
                    list.push_back(to_json(issue));
                }
                return json_response(422, {{"detail", std::move(list)}});
            }
            case What::problem: {
                const int code = problem.status && *problem.status >= 100 && *problem.status <= 999
                                     ? *problem.status
                                     : 500;
                return json_response(code, to_json(problem));
            }
        }
    }
    switch (what) {
        case What::status:
            return http_problem(status, message);
        case What::invalid:
            return validation_problem(issues);
        case What::problem: {
            const auto [code, body] = wot_problem(problem);
            return problem_response(code, body);
        }
        case What::typed:
            break;
    }
    return problem_response(
        wot_status,
        {{"type", problem_type(name)}, {"title", title}, {"status", wot_status}, {"detail", message}});
}

Response http_problem(int status, const std::string& message) {
    const std::string_view reason = reason_phrase(status);
    return problem_response(status, {{"type", "about:blank"},
                                     {"title", reason.empty() ? "Error" : std::string(reason)},
                                     {"status", status},
                                     {"detail", message}});
}

json::Value invalid_param(const ValidationIssue& issue) {
    std::string location = "body";
    std::size_t first = 0;
    if (!issue.loc.empty() && !issue.loc.front().is_index()) {
        const std::string key = issue.loc.front().to_string();
        if (key == "body" || key == "path" || key == "query") {
            location = key;
            first = 1;
        }
    }
    std::string name;
    for (std::size_t i = first; i < issue.loc.size(); ++i) {
        std::string part = issue.loc[i].to_string();
        if (location == "body") {
            // A JSON Pointer.
            std::string escaped;
            for (const char c : part) {
                escaped += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
            }
            name += "/" + escaped;
        } else {
            name += (i == first ? "" : ".") + part;
        }
    }
    return {{"in", location}, {"name", name}, {"reason", issue.msg}, {"code", issue.type}};
}

std::pair<int, json::Value> wot_problem(const ProblemDetails& problem) {
    auto tetathing = [&](std::string_view exception) {
        return problem.type && *problem.type == teta_wot_exception_type(exception);
    };
    const char* name = "failed";
    const char* title = "Failed";
    int status = 500;
    if (tetathing("GlobalLockBusyError")) {
        name = "global-lock-busy";
        title = "Global lock busy";
        status = 503;
    } else if (tetathing("InvocationCancelledError")) {
        name = "cancelled";
        title = "Cancelled";
        status = 503;
    } else if (!tetathing("InvocationError") && problem.type &&
               !problem.type->starts_with(teta_wot_exceptions_url)) {
        // A type the Thing chose: kept, with its own title and status.
        const int code =
            problem.status && *problem.status >= 100 && *problem.status <= 999 ? *problem.status : 500;
        json::Value body = json::Value::object();
        body["type"] = *problem.type;
        if (problem.title) {
            body["title"] = *problem.title;
        }
        body["status"] = code;
        if (problem.detail) {
            body["detail"] = *problem.detail;
        }
        if (problem.instance) {
            body["instance"] = *problem.instance;
        }
        return {code, std::move(body)};
    }
    json::Value body = json::Value::object();
    body["type"] = problem_type(name);
    body["title"] = title;
    body["status"] = status;
    if (problem.detail) {
        body["detail"] = *problem.detail;
    }
    if (problem.title && *problem.title != title) {
        body["error"] = *problem.title;
    }
    if (problem.instance) {
        body["instance"] = *problem.instance;
    }
    return {status, std::move(body)};
}

// ---- CORS ---------------------------------------------------------------------------------

Response preflight(std::string_view origin, std::optional<std::string_view> requested_headers) {
    Response response = Response::text("OK");
    response.headers.set("access-control-allow-origin", std::string(origin));
    response.headers.set("access-control-allow-credentials", "true");
    response.headers.set("access-control-allow-methods", std::string(all_methods));
    if (requested_headers) {
        response.headers.set("access-control-allow-headers", std::string(*requested_headers));
    }
    response.headers.set("access-control-max-age", "600");
    response.headers.set("vary", std::string(preflight_vary));
    return response;
}

void add_cors(Response& response, const std::optional<std::string>& origin) {
    if (const auto vary = response.headers.get("vary"); vary && is_header_text(*vary)) {
        response.headers.set("vary", std::string(*vary) + ", Origin");
    } else {
        response.headers.set("vary", "Origin");
    }
    if (origin) {
        response.headers.set("access-control-allow-origin", *origin);
        response.headers.set("access-control-allow-credentials", "true");
    }
}

}  // namespace teta::wot::http::detail
