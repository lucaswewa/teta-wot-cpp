// Validation: the input is a JSON document `{"schema": ..., "input": ...}`.
// For any schema the TD model reads, and any input, the validator returns a
// value or throws ValidationError, whose issues are pydantic's shape (a type,
// a message, a location under `body`); a value it returns validates again,
// unchanged (coercion is idempotent).
//
// Byte mutations mostly break the document, so each document that parses is
// also tried with its input replaced, whole and member by member, by awkward
// values.

#include <optional>
#include <string>
#include <vector>

#include "fuzz.hpp"
#include "teta/wot/json.hpp"
#include "teta/wot/td/data_schema.hpp"
#include "teta/wot/validate.hpp"

namespace tw = teta::wot;
namespace json = teta::wot::json;

namespace {

const json::Value& awkward() {
    static const auto values = json::parse(R"json([
        null, true, false, 0, -1, 1, 1.5, -0.0, 1e300, 18446744073709551615, -9223372036854775808,
        "", "1", " 2 ", "1.0", "1e3", "true", "yes", "nan", "inf", "x", "é😀",
        [], [1], [[1, 2], [3]], [null], {}, {"x": 1}, {"": null}
    ])json");
    return values;
}

void check_one(const tw::SchemaValidator& validator, const json::Value& value) {
    const auto where = json::dump(tw::td::to_json(validator.schema())) + " with " + json::dump(value);
    json::Value output;
    try {
        output = validator.validate(value, {"body"});
    } catch (const tw::ValidationError& error) {
        fuzz::check(!error.issues().empty(), "a ValidationError without issues: " + where);
        for (const auto& issue : error.issues()) {
            fuzz::check(!issue.type.empty() && !issue.msg.empty(),
                        "an issue without a type or message: " + where);
            fuzz::check(!issue.loc.empty() && issue.loc.front() == tw::LocItem("body"),
                        "an issue that isn't under `body`: " + where);
            (void)json::dump(tw::to_json(issue));
        }
        return;
    }
    try {
        const auto again = validator.validate(output, {"body"});
        fuzz::check(json::dump(again) == json::dump(output),
                    "validating the output changes it, to " + json::dump(again) + ": " + where);
    } catch (const tw::ValidationError& error) {
        throw fuzz::Failure("the output, " + json::dump(output) + ", doesn't validate (" + error.what() +
                            "): " + where);
    }
}

}  // namespace

void fuzz::one_input(std::string_view input) {
    json::Value document;
    try {
        document = json::parse(input);
    } catch (const json::ParseError&) {
        return;
    }
    if (!document.is_object() || !document.contains("schema") || !document.contains("input")) {
        return;
    }
    std::optional<tw::SchemaValidator> validator;
    try {
        validator.emplace(tw::td::data_schema_from_json(document["schema"]));
    } catch (const tw::td::InvalidDataSchema&) {
        return;
    } catch (const tw::InvalidPattern&) {
        return;
    }
    const auto& value = document["input"];
    check_one(*validator, value);
    for (const auto& replacement : awkward()) {
        check_one(*validator, replacement);
        if (value.is_array() || value.is_object()) {
            for (auto member = value.begin(); member != value.end(); ++member) {
                auto changed = value;
                if (value.is_array()) {
                    changed[static_cast<std::size_t>(member - value.begin())] = replacement;
                } else {
                    changed[member.key()] = replacement;
                }
                check_one(*validator, changed);
            }
        }
    }
}
