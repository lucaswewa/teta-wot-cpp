#include "teta/wot/td/validation.hpp"

#include <cstddef>
#include <nlohmann/json-schema.hpp>

namespace teta::wot::embedded {
extern const unsigned char td_json_schema[];
extern const std::size_t td_json_schema_size;
}  // namespace teta::wot::embedded

namespace teta::wot::td::validation {

namespace {

// Collects every error, rather than stopping at the first.
class Collector : public nlohmann::json_schema::basic_error_handler {
  public:
    void error(const nlohmann::json::json_pointer& pointer, const nlohmann::json& instance,
               const std::string& message) override {
        basic_error_handler::error(pointer, instance, message);
        violations.push_back({pointer.to_string(), message});
    }
    std::vector<SchemaViolation> violations;
};

const nlohmann::json_schema::json_validator& validator() {
    static const nlohmann::json_schema::json_validator instance = [] {
        nlohmann::json_schema::json_validator v(nullptr, nlohmann::json_schema::default_string_format_check);
        v.set_root_schema(nlohmann::json::parse(td_json_schema()));
        return v;
    }();
    return instance;
}

std::string list(const std::vector<SchemaViolation>& violations) {
    std::string out = "the TD doesn't match the W3C TD 1.1 JSON Schema:";
    for (const auto& v : violations) {
        out += "\n- " + v.message + " at " + (v.instance_path.empty() ? std::string("/") : v.instance_path);
    }
    return out;
}

}  // namespace

std::string_view td_json_schema() {
    return {reinterpret_cast<const char*>(embedded::td_json_schema), embedded::td_json_schema_size};
}

SchemaValidationError::SchemaValidationError(std::vector<SchemaViolation> found)
    : std::runtime_error(list(found)), violations_(std::move(found)) {}

std::vector<SchemaViolation> violations(const json::Value& td) {
    Collector collector;
    // The validator works on nlohmann's unordered JSON; order doesn't matter here.
    validator().validate(nlohmann::json::parse(json::dump(td)), collector);
    return std::move(collector.violations);
}

void validate_json(const json::Value& td) {
    auto found = violations(td);
    if (!found.empty()) {
        throw SchemaValidationError(std::move(found));
    }
}

void validate_schema(const ThingDescription& td) {
    validate_json(to_json(td));
}

}  // namespace teta::wot::td::validation
