// The validator against the reference's 422 answers in
// conformance/fixtures/http/validation.json: every property write and
// invocation the reference refused for its value gets the same errors from
// SchemaValidator, given the affordance's DataSchema from the twins' TDs
// (fixtures/http/tds.json). The HTTP layer itself (a `null` or empty body
// read as missing, `json_invalid`) is Phase 4's, so those steps are skipped.

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "teta/wot/td/thing.hpp"
#include "teta/wot/validate.hpp"

namespace {

namespace tw = teta::wot;
using tw::json::Value;

Value fixture(const std::string& name) {
    std::ifstream file(std::string(TETA_WOT_FIXTURES) + "/http/" + name, std::ios::binary);
    EXPECT_TRUE(file) << name;
    return Value::parse(file);
}

// The TD of each Thing in tds.json, by its name in the configuration.
Value thing_description(const Value& tds, const std::string& thing) {
    for (const auto& step : tds["steps"]) {
        if (step["step"]["path"].get<std::string>() == "/" + thing + "/") {
            return step["response"]["body"];
        }
    }
    ADD_FAILURE() << "no TD for " << thing;
    return nullptr;
}

TEST(HttpValidation, TheReferencesRefusalsAreTheValidatorsErrors) {
    const auto tds = fixture("tds.json");
    const auto scenario = fixture("validation.json");
    int checked = 0;
    for (const auto& entry : scenario["steps"]) {
        const auto& step = entry["step"];
        const auto& response = entry["response"];
        if (response["status"].get<int>() != 422 || !step.contains("json") || step["json"].is_null()) {
            continue;
        }
        const auto path = step["path"].get<std::string>();
        const auto slash = path.find('/', 1);
        const auto td = thing_description(tds, path.substr(1, slash - 1));
        const auto name = path.substr(slash + 1);
        const auto method = step["method"].get<std::string>();
        const auto schema = method == "PUT" ? tw::td::property_from_json(td["properties"][name]).schema
                                            : tw::td::action_from_json(td["actions"][name]).input.value();
        std::string actual;
        try {
            const auto value = tw::SchemaValidator(schema).validate(step["json"], {"body"});
            actual = "validated: " + tw::json::dump(value);
        } catch (const tw::ValidationError& error) {
            Value detail = Value::array();
            for (const auto& issue : error.issues()) {
                detail.push_back(tw::to_json(issue));
            }
            actual = tw::json::dump(Value{{"detail", detail}});
        }
        EXPECT_EQ(actual, response["text"].get<std::string>()) << step["name"].get<std::string>();
        ++checked;
    }
    EXPECT_GE(checked, 30);
}

}  // namespace
