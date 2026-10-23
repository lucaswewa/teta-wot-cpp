#include "discovery.hpp"

#include <algorithm>
#include <cstdint>
#include <map>

#include "teta/wot/td/describe.hpp"

namespace teta::wot::http::detail {

namespace {

/// The Discovery context.
constexpr std::string_view discovery_context = "https://www.w3.org/2022/wot/discovery";

/// Text with invalid UTF-8 replaced by U+FFFD, a maximal invalid subpart at a
/// time (Rust's `String::from_utf8_lossy`).
std::string utf8_lossy(std::string_view bytes) {
    constexpr std::string_view replacement = "\xef\xbf\xbd";
    std::string out;
    std::size_t i = 0;
    const auto at = [&](std::size_t k) { return static_cast<unsigned char>(bytes[k]); };
    while (i < bytes.size()) {
        const unsigned char lead = at(i);
        if (lead < 0x80) {
            out += static_cast<char>(lead);
            ++i;
            continue;
        }
        std::size_t length = 0;
        unsigned char low = 0x80;
        unsigned char high = 0xbf;
        if (lead >= 0xc2 && lead <= 0xdf) {
            length = 2;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            length = 3;
            low = lead == 0xe0 ? 0xa0 : 0x80;
            high = lead == 0xed ? 0x9f : 0xbf;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            length = 4;
            low = lead == 0xf0 ? 0x90 : 0x80;
            high = lead == 0xf4 ? 0x8f : 0xbf;
        }
        if (length == 0) {
            out += replacement;
            ++i;
            continue;
        }
        std::size_t valid = 1;
        while (valid < length && i + valid < bytes.size()) {
            const unsigned char next = at(i + valid);
            const bool ok = valid == 1 ? next >= low && next <= high : next >= 0x80 && next <= 0xbf;
            if (!ok) {
                break;
            }
            ++valid;
        }
        if (valid == length) {
            out.append(bytes.substr(i, length));
        } else {
            out += replacement;
        }
        i += valid;
    }
    return out;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/// `%XX` escapes decoded, as bytes. In a form (the query, as
/// `form_urlencoded` reads it), `+` is a space; in a path (teta-wot's own
/// decoding, with `u8::from_str_radix`), `%+X` is the byte X too.
std::string decode_bytes(std::string_view raw, bool form) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
            const bool signed_digit = !form && raw[i + 1] == '+';
            const int high = signed_digit ? 0 : hex_digit(raw[i + 1]);
            const int low = hex_digit(raw[i + 2]);
            if (high >= 0 && low >= 0) {
                out += static_cast<char>(high * 16 + low);
                i += 2;
                continue;
            }
        }
        out += form && raw[i] == '+' ? ' ' : raw[i];
    }
    return out;
}

/// The query's arguments, as serde reads them into a map: `application/
/// x-www-form-urlencoded`, empty pairs skipped, the last of a repeated name
/// kept.
std::map<std::string, std::string> query_arguments(const std::optional<std::string>& query) {
    std::map<std::string, std::string> arguments;
    if (!query) {
        return arguments;
    }
    std::string_view rest = *query;
    while (!rest.empty()) {
        const auto amp = rest.find('&');
        const auto pair = rest.substr(0, amp);
        rest = amp == std::string_view::npos ? std::string_view() : rest.substr(amp + 1);
        if (pair.empty()) {
            continue;
        }
        const auto equals = pair.find('=');
        const auto name = pair.substr(0, equals);
        const auto value = equals == std::string_view::npos ? std::string_view() : pair.substr(equals + 1);
        arguments[utf8_lossy(decode_bytes(name, true))] = utf8_lossy(decode_bytes(value, true));
    }
    return arguments;
}

/// A whole number as Rust's `usize::from_str` reads it: an optional `+`,
/// then digits, without overflow.
std::optional<std::uint64_t> parse_usize(std::string_view text) {
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value;
}

/// 400 for a query argument the listing can't use.
Response bad_query(std::string_view name, std::string_view reason) {
    return problem_response(
        400, json::Value{
                 {"type", problem_type("validation-error")},
                 {"title", "Validation error"},
                 {"status", 400},
                 {"detail", "Invalid query argument `" + std::string(name) + "`: " + std::string(reason)},
                 {"invalid-params",
                  json::Value::array({json::Value{{"in", "query"}, {"name", name}, {"reason", reason}}})}});
}

std::string id_of(const json::Value& td) {
    const auto id = td.find("id");
    return id != td.end() && id->is_string() ? id->get<std::string>() : std::string();
}

/// The collection's state, for the `canonical` link: it changes when the
/// TDs' IDs do.
std::string etag(const std::vector<json::Value>& tds) {
    std::string ids;
    bool first = true;
    for (const auto& td : tds) {
        const auto id = td.find("id");
        if (id == td.end() || !id->is_string()) {
            continue;
        }
        if (!first) {
            ids += '\n';
        }
        ids += id->get<std::string>();
        first = false;
    }
    auto uuid = td::url_uuid_v5(ids);
    std::erase(uuid, '-');
    return uuid;
}

}  // namespace

std::string directory_path(const Urls& urls) {
    return urls.prefix + "/directory/";
}

json::Value directory_td(const HttpOptions& options, const Urls& urls) {
    json::Value td = json::parse(R"({
        "@context": ["https://www.w3.org/2022/wot/td/v1.1", "https://www.w3.org/2022/wot/discovery"],
        "@type": "ThingDirectory",
        "id": "",
        "title": "Thing Description Directory",
        "description": "",
        "base": "",
        "securityDefinitions": {"nosec_sc": {"scheme": "nosec"}},
        "security": "nosec_sc",
        "properties": {
            "things": {
                "description": "Retrieve all Thing Descriptions",
                "readOnly": true,
                "uriVariables": {
                    "offset": {
                        "title": "Number of TDs to skip before the page",
                        "type": "number",
                        "default": 0
                    },
                    "limit": {"title": "Number of TDs in a page", "type": "number"},
                    "format": {
                        "title": "Payload format",
                        "type": "string",
                        "enum": ["array", "collection"],
                        "default": "array"
                    }
                },
                "forms": [{
                    "href": "things{?offset,limit,format}",
                    "op": "readproperty",
                    "htv:methodName": "GET",
                    "response": {
                        "description": "Success response",
                        "htv:statusCodeValue": 200,
                        "contentType": "application/ld+json",
                        "htv:headers": [{"htv:fieldName": "Link"}]
                    },
                    "additionalResponses": [{
                        "description": "Invalid query arguments",
                        "contentType": "application/problem+json",
                        "htv:statusCodeValue": 400
                    }]
                }]
            }
        },
        "actions": {
            "retrieveThing": {
                "description": "Retrieve a Thing Description",
                "uriVariables": {
                    "id": {
                        "@type": "ThingID",
                        "title": "Thing Description ID",
                        "type": "string",
                        "format": "iri-reference"
                    }
                },
                "output": {
                    "description": "The schema is implied by the content type",
                    "type": "object"
                },
                "safe": true,
                "idempotent": true,
                "synchronous": true,
                "forms": [{
                    "href": "things/{id}",
                    "op": "invokeaction",
                    "htv:methodName": "GET",
                    "response": {
                        "description": "Success response",
                        "htv:statusCodeValue": 200,
                        "contentType": "application/td+json"
                    },
                    "additionalResponses": [{
                        "description": "TD with the given id not found",
                        "contentType": "application/problem+json",
                        "htv:statusCodeValue": 404
                    }]
                }]
            }
        }
    })");
    td["id"] = td::thing_id(options.server_id, "directory");
    td["description"] =
        "The Things served by " + options.api_title + ": a read-only Thing Description Directory.";
    td["base"] = urls.absolute(directory_path(urls));
    return td;
}

Response json_as(std::string_view media_type, const json::Value& value) {
    Response response = json_response(200, value);
    response.headers.set("content-type", std::string(media_type));
    return response;
}

Response listing(std::vector<json::Value> tds, const std::optional<std::string>& query, const Urls& urls) {
    const auto arguments = query_arguments(query);
    std::optional<std::uint64_t> offset;
    std::optional<std::uint64_t> limit;
    for (const char* name : {"offset", "limit"}) {
        const auto found = arguments.find(name);
        if (found == arguments.end()) {
            continue;
        }
        const auto number = parse_usize(found->second);
        if (!number) {
            return bad_query(name, "must be a whole number, 0 or more");
        }
        (std::string_view(name) == "offset" ? offset : limit) = number;
    }
    if (limit == 0u) {
        return bad_query("limit", "must be a positive whole number");
    }
    bool collection = false;
    if (const auto format = arguments.find("format"); format != arguments.end()) {
        if (format->second == "collection") {
            collection = true;
        } else if (format->second != "array") {
            return bad_query("format", "must be `array` or `collection`");
        }
    }

    // Sorted by `id`, in code point order (byte order, in UTF-8).
    std::ranges::stable_sort(tds, {}, id_of);
    const std::uint64_t total = tds.size();
    const std::uint64_t start = std::min(offset.value_or(0), total);
    const std::uint64_t end = limit ? std::min(start + std::min(*limit, total), total) : total;
    json::Value page = json::Value::array();
    for (auto i = start; i < end; ++i) {
        page.push_back(tds[static_cast<std::size_t>(i)]);
    }

    const std::string path = directory_path(urls) + "things";
    const auto page_url = [&](std::uint64_t at) {
        std::string url = path + "?offset=" + std::to_string(at);
        if (limit) {
            url += "&limit=" + std::to_string(*limit);
        }
        if (collection) {
            url += "&format=collection";
        }
        return urls.absolute(url);
    };
    const std::optional<std::string> next = end < total ? std::optional(page_url(end)) : std::nullopt;
    json::Value body;
    if (collection) {
        body = json::Value{{"@context", discovery_context},
                           {"@type", "ThingCollection"},
                           {"total", total},
                           {"members", std::move(page)},
                           {"@id", page_url(start)}};
        if (next) {
            body["next"] = *next;
        }
    } else {
        body = std::move(page);
    }
    Response response = json_as("application/ld+json", body);
    if (next) {
        response.headers.add("link", "<" + *next + ">; rel=\"next\"");
    }
    if (offset || limit) {
        response.headers.add("link",
                             "<" + urls.absolute(path) + ">; rel=\"canonical\"; etag=\"" + etag(tds) + "\"");
    }
    return response;
}

Response retrieve(const std::vector<json::Value>& tds, std::string_view raw_id) {
    const std::string id = percent_decode(raw_id);
    for (const auto& td : tds) {
        if (const auto found = td.find("id"); found != td.end() && found->is_string() && *found == id) {
            return json_as("application/td+json", td);
        }
    }
    return http_problem(404, "No Thing Description has the ID " + id);
}

std::string percent_decode(std::string_view raw) {
    return utf8_lossy(decode_bytes(raw, false));
}

}  // namespace teta::wot::http::detail
