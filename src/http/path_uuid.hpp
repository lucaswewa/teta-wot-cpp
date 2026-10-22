// Reading invocation IDs from paths as teta-wot does, with the `uuid` crate's
// `Uuid::parse_str` (1.26.1): the simple, hyphenated, braced and URN forms,
// in either case, and its error messages, which teta-wot puts in its 422s.
// `tests/http/wire_corpus.jsonl` (`wire-corpus`) is the specification.

#pragma once

#include <string>
#include <string_view>
#include <variant>

#include "teta/wot/uuid.hpp"

namespace teta::wot::http::detail {

/// The UUID, or why `text` isn't one (`invalid character: found `i` at 4`).
std::variant<Uuid, std::string> parse_path_uuid(std::string_view text);

}  // namespace teta::wot::http::detail
