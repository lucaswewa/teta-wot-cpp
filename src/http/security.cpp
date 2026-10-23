#include "security.hpp"

#include "render.hpp"

namespace teta::wot::http::detail {

namespace {

/// Compares without stopping at the first difference, so that the time taken
/// doesn't tell how much of a guess was right.
bool constant_time_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

}  // namespace

std::pair<std::string, td::SecurityScheme> security_scheme(const Security& security) {
    if (security.scheme == Security::Scheme::basic) {
        auto scheme = td::SecurityScheme::basic().with_description("HTTP Basic authentication (RFC 7617)");
        // The WoT Profile wants both, with these values.
        scheme.location = td::CredentialLocation::header;
        scheme.name = "Authorization";
        return {"basic_sc", std::move(scheme)};
    }
    auto scheme = td::SecurityScheme::bearer().with_description("A bearer token (RFC 6750)");
    scheme.location = td::CredentialLocation::header;
    scheme.name = "Authorization";
    // Not a JWT, TD 1.1's default: the server compares the token.
    scheme.format = "opaque";
    return {"bearer_sc", std::move(scheme)};
}

bool authorized(const Security& security, const Headers& headers) {
    const bool basic = security.scheme == Security::Scheme::basic;
    const std::string_view scheme = basic ? "basic" : "bearer";
    const std::string expected = basic ? base64(security.username + ":" + security.secret) : security.secret;
    for (const auto value : headers.get_all("authorization")) {
        const auto given = trim(value);
        const auto space = given.find(' ');
        if (space == std::string_view::npos) {
            continue;
        }
        if (equals_ignoring_case(given.substr(0, space), scheme) &&
            constant_time_equal(trim(given.substr(space + 1)), expected)) {
            return true;
        }
    }
    return false;
}

Response unauthorized(const Security& security, WireProfile profile) {
    Response response = HttpError::with_status(401, "Not authenticated").response(profile);
    const std::string realm(security_realm);
    response.headers.set("www-authenticate", security.scheme == Security::Scheme::basic
                                                 ? "Basic realm=\"" + realm + "\", charset=\"UTF-8\""
                                                 : "Bearer realm=\"" + realm + "\"");
    return response;
}

std::string base64(std::string_view bytes) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t chunk = std::min<std::size_t>(3, bytes.size() - i);
        std::uint32_t n = 0;
        for (std::size_t j = 0; j < chunk; ++j) {
            n |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + j])) << (16 - 8 * j);
        }
        for (std::size_t j = 0; j < 4; ++j) {
            out += j <= chunk ? alphabet[(n >> (18 - 6 * j)) & 63] : '=';
        }
    }
    return out;
}

}  // namespace teta::wot::http::detail
