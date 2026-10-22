#include <algorithm>

#include "teta/wot/http/message.hpp"

namespace teta::wot::http {

bool equals_ignoring_case(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        return lower(x) == lower(y);
    });
}

std::optional<std::string_view> Headers::get(std::string_view name) const {
    for (const auto& [field, value] : fields_) {
        if (equals_ignoring_case(field, name)) {
            return value;
        }
    }
    return std::nullopt;
}

std::vector<std::string_view> Headers::get_all(std::string_view name) const {
    std::vector<std::string_view> out;
    for (const auto& [field, value] : fields_) {
        if (equals_ignoring_case(field, name)) {
            out.emplace_back(value);
        }
    }
    return out;
}

Headers& Headers::add(std::string name, std::string value) {
    fields_.emplace_back(std::move(name), std::move(value));
    return *this;
}

Headers& Headers::set(std::string name, std::string value) {
    const auto first =
        std::ranges::find_if(fields_, [&](const Field& f) { return equals_ignoring_case(f.first, name); });
    if (first == fields_.end()) {
        return add(std::move(name), std::move(value));
    }
    first->second = std::move(value);
    // Later fields of the same name go.
    const auto rest = std::remove_if(first + 1, fields_.end(),
                                     [&](const Field& f) { return equals_ignoring_case(f.first, name); });
    fields_.erase(rest, fields_.end());
    return *this;
}

void Headers::remove(std::string_view name) {
    std::erase_if(fields_, [&](const Field& f) { return equals_ignoring_case(f.first, name); });
}

Request Request::make(std::string method, std::string_view target) {
    Request request;
    request.method = std::move(method);
    if (const auto scheme_end = target.find("://");
        scheme_end != std::string_view::npos && target[0] != '/') {
        request.scheme = std::string(target.substr(0, scheme_end));
        target.remove_prefix(scheme_end + 3);
        const auto path_start = target.find_first_of("/?");
        request.authority = std::string(target.substr(0, path_start));
        target = path_start == std::string_view::npos ? std::string_view("/") : target.substr(path_start);
    }
    if (const auto question = target.find('?'); question != std::string_view::npos) {
        request.query = std::string(target.substr(question + 1));
        target = target.substr(0, question);
    }
    request.path = target.empty() ? "/" : std::string(target);
    return request;
}

std::string Request::target() const {
    return query ? path + "?" + *query : path;
}

Response Response::json(int status, const teta::wot::json::Value& value) {
    return bytes(teta::wot::json::dump(value), "application/json", status);
}

Response Response::text(std::string text, int status) {
    return bytes(std::move(text), "text/plain; charset=utf-8", status);
}

Response Response::bytes(std::string body, std::string content_type, int status) {
    Response response;
    response.status = status;
    response.headers.set("content-type", std::move(content_type));
    response.body = std::move(body);
    return response;
}

Response Response::empty(int status) {
    Response response;
    response.status = status;
    return response;
}

std::string_view reason_phrase(int status) {
    switch (status) {
        case 100:
            return "Continue";
        case 101:
            return "Switching Protocols";
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 202:
            return "Accepted";
        case 204:
            return "No Content";
        case 206:
            return "Partial Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 304:
            return "Not Modified";
        case 307:
            return "Temporary Redirect";
        case 308:
            return "Permanent Redirect";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 410:
            return "Gone";
        case 411:
            return "Length Required";
        case 412:
            return "Precondition Failed";
        case 413:
            return "Payload Too Large";
        case 414:
            return "URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 416:
            return "Range Not Satisfiable";
        case 422:
            return "Unprocessable Entity";
        case 429:
            return "Too Many Requests";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        default:
            return "";
    }
}

}  // namespace teta::wot::http
