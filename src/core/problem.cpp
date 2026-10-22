#include "teta/wot/problem.hpp"

namespace teta::wot {

std::string teta_wot_exception_type(std::string_view exception) {
    return std::string(teta_wot_exceptions_url) + "#teta_wot.exceptions." + std::string(exception);
}

ProblemDetails ProblemDetails::teta_wot_things(std::string_view exception, std::string detail,
                                               std::uint16_t status) {
    return {std::move(detail), teta_wot_exception_type(exception), status, std::string(exception),
            std::nullopt};
}

ProblemDetails ProblemDetails::untyped(std::string title, std::string detail, std::uint16_t status) {
    return {std::move(detail), std::nullopt, status, std::move(title), std::nullopt};
}

json::Value to_json(const ProblemDetails& problem) {
    auto optional = [](const auto& value) -> json::Value {
        if (value) {
            return json::Value(*value);
        }
        return nullptr;
    };
    return {{"detail", optional(problem.detail)},
            {"type", optional(problem.type)},
            {"status", optional(problem.status)},
            {"title", optional(problem.title)},
            {"instance", optional(problem.instance)}};
}

}  // namespace teta::wot
