// RFC 9457 problem details: a port of teta-wot-core's `problem`.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "teta/wot/json.hpp"

namespace teta::wot {

/// Where teta-wot documents its exceptions; problem `type` URIs for them
/// point here.
inline constexpr std::string_view teta_wot_exceptions_url =
    "https://www.tetaprecision.com/api/exceptions/index.html";

/// The `type` URI of one of teta-wot's exceptions, such as
/// `GlobalLockBusyError`.
std::string teta_wot_exception_type(std::string_view exception);

/// A problem details object: `{detail, type, status, title, instance}`, every
/// member written, `null` when absent.
struct ProblemDetails {
    std::optional<std::string> detail;
    std::optional<std::string> type;
    std::optional<std::uint16_t> status;
    std::optional<std::string> title;
    std::optional<std::string> instance;

    /// A problem named after one of teta-wot's exceptions, with its `type`.
    static ProblemDetails teta_wot_things(std::string_view exception, std::string detail,
                                          std::uint16_t status);
    /// A problem with no `type`.
    static ProblemDetails untyped(std::string title, std::string detail, std::uint16_t status);

    bool operator==(const ProblemDetails&) const = default;
};

json::Value to_json(const ProblemDetails& problem);

}  // namespace teta::wot
