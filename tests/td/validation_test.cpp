// Validation against the W3C TD 1.1 JSON Schema: teta-wot-td's tests, ported,
// and every TD the reference serves (Phase 1 gate: every TD is valid).

#include "teta/wot/td/validation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>

#include "teta/wot/td/describe.hpp"

namespace {

namespace td = teta::wot::td;
namespace validation = teta::wot::td::validation;
using teta::wot::json::Value;

Value minimal_td() {
    return {{"@context", td::td_context_v1_1},
            {"title", "Example Thing"},
            {"security", "nosec_sc"},
            {"securityDefinitions", {{"nosec_sc", {{"scheme", "nosec"}}}}}};
}

bool has_path(const std::vector<validation::SchemaViolation>& violations, const std::string& path) {
    return std::ranges::any_of(violations, [&](const auto& v) { return v.instance_path == path; });
}

TEST(Validation, VendoredSchemaParsesAndAcceptsAMinimalTd) {
    const auto schema = Value::parse(validation::td_json_schema());
    EXPECT_EQ(schema["version"], "1.1-12-March-2025");
    EXPECT_EQ(schema["$id"], "https://www.w3.org/2022/wot/td-schema/v1.1");
    EXPECT_NO_THROW(validation::validate_json(minimal_td()));
}

TEST(Validation, ReportsRequiredFieldsAndNestedPaths) {
    const auto missing = validation::violations(Value::object());
    EXPECT_FALSE(missing.empty());
    EXPECT_TRUE(std::ranges::all_of(missing, [](const auto& v) { return v.instance_path.empty(); }));

    auto td = minimal_td();
    td["title"] = 42;
    td["created"] = "not-a-date";
    const auto invalid = validation::violations(td);
    EXPECT_TRUE(has_path(invalid, "/title"));
    EXPECT_TRUE(has_path(invalid, "/created"));
    try {
        validation::validate_json(td);
        FAIL() << "no error";
    } catch (const validation::SchemaValidationError& error) {
        EXPECT_NE(std::string(error.what()).find("the TD doesn't match"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("/title"), std::string::npos);
    }
}

TEST(Validation, RejectsAnInvalidPropertyAffordance) {
    auto td = minimal_td();
    td["properties"] = {{"temperature", {{"type", "number"}}}};
    const auto found = validation::violations(td);
    // json-schema-validator reports a failing `additionalProperties` member at
    // its parent, naming it in the message (Rust's `jsonschema` reports it at
    // `/properties/temperature`).
    EXPECT_TRUE(std::ranges::any_of(found, [](const auto& v) {
        return v.instance_path.starts_with("/properties") &&
               v.message.find("temperature") != std::string::npos;
    }));
}

TEST(Validation, ValidatesABuiltTd) {
    auto td =
        td::ThingBuilder("Temperature Sensor")
            .property("temperature",
                      td::PropertyBuilder(td::DataSchema::of(td::DataType::number))
                          .form(td::Form("/temperature")
                                    .with_op(td::OneOrMany<td::Operation>(td::Operation::readproperty))))
            .build();
    EXPECT_NO_THROW(validation::validate_schema(td));
    td.created = "not-a-date";
    EXPECT_THROW(validation::validate_schema(td), validation::SchemaValidationError);
}

// Every TD the reference serves, in both profiles, is valid: the TDs the C++
// side must reproduce (TwinTds) are valid by construction.
TEST(Validation, EveryReferenceTdIsValid) {
    for (const char* fixture : {"tds.json", "tds-wot.json"}) {
        std::ifstream file(std::string(TETA_WOT_FIXTURES) + "/http/" + fixture, std::ios::binary);
        ASSERT_TRUE(file);
        const auto data = Value::parse(file);
        for (const auto& step : data["steps"]) {
            // Masked UUIDs would break `id`'s URI format: use a real one.
            auto td = Value::parse(step["response"]["text"].get<std::string>());
            td["id"] = "urn:uuid:f46f31e7-a858-5fd5-91b0-829d229ed6cd";
            const auto found = validation::violations(td);
            EXPECT_TRUE(found.empty())
                << fixture << " " << step["step"]["name"] << ": "
                << (found.empty() ? "" : found[0].instance_path + " " + found[0].message);
        }
    }
}

}  // namespace
