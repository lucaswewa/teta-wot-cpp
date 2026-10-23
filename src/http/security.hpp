// Optional credentials: a port of teta-wot-http's `security.rs`. HTTP Basic
// or a bearer token, required by every interaction and described in the TDs.
// Descriptions (TDs, the directory, the OpenAPI document and the docs) stay
// public.

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "teta/wot/http/binding.hpp"
#include "teta/wot/td/form.hpp"

namespace teta::wot::http::detail {

/// The realm named in `WWW-Authenticate`.
inline constexpr std::string_view security_realm = "teta-wot";

/// The TD's security definition for `security`: its name and scheme.
std::pair<std::string, td::SecurityScheme> security_scheme(const Security& security);

/// Whether the request carries the credentials (any `Authorization` header
/// with the scheme, in any case, and the credentials).
bool authorized(const Security& security, const Headers& headers);

/// 401, with the scheme in `WWW-Authenticate`; its body is the profile's
/// (FastAPI's for its security helpers in `tetathing`).
Response unauthorized(const Security& security, WireProfile profile);

/// Standard base64, with padding (RFC 4648).
std::string base64(std::string_view bytes);

}  // namespace teta::wot::http::detail
