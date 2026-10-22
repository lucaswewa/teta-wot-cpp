// The reference's answers in conformance/fixtures/http/validation.json,
// replayed in-process on the C++ twins: every property write, property read
// and invocation the scenario makes goes through the twin's PropertyEntry or
// ActionEntry, and gets the reference's outcome: the same 422 errors, byte
// for byte, or success, with the same values read back. What only the HTTP
// layer does (a `null` or empty body read as missing, malformed JSON, the
// invocation resources) is Phase 4's, so those steps are skipped.

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "teta/wot.hpp"
#include "twins.hpp"

namespace {

namespace tw = teta::wot;
using tw::json::Value;

Value fixture(const std::string& name) {
    std::ifstream file(std::string(TETA_WOT_FIXTURES) + "/http/" + name, std::ios::binary);
    EXPECT_TRUE(file) << name;
    return Value::parse(file);
}

std::string detail(const tw::ValidationError& error) {
    Value issues = Value::array();
    for (const auto& issue : error.issues()) {
        issues.push_back(tw::to_json(issue));
    }
    return tw::json::dump(Value{{"detail", issues}});
}

TEST(HttpValidation, TheTwinsGiveTheReferencesOutcomes) {
    const auto scenario = fixture("validation.json");
    tw::RuntimeBuilder builder;
    for (const auto& [name, type] : scenario["config"]["things"].items()) {
        (void)type;
        twins::add(builder, name);
    }
    const auto runtime = builder.build();
    int checked = 0;
    for (const auto& entry : scenario["steps"]) {
        const auto& step = entry["step"];
        const auto& response = entry["response"];
        const auto name = step["name"].get<std::string>();
        const auto path = step["path"].get<std::string>();
        const auto method = step["method"].get<std::string>();
        const auto slash = path.find('/', 1);
        const auto* thing = runtime.thing(path.substr(1, slash - 1));
        if (!thing) {
            continue;  // /action_invocations/...
        }
        const auto affordance = path.substr(slash + 1);
        const int status = response["status"].get<int>();
        if (method == "GET") {
            EXPECT_EQ(tw::json::dump(thing->property(affordance)->read()),
                      response["text"].get<std::string>())
                << name;
            ++checked;
            continue;
        }
        if (!step.contains("json") || step["json"].is_null()) {
            continue;  // the HTTP layer's
        }
        std::string outcome = "accepted";
        try {
            if (method == "PUT") {
                thing->property(affordance)->write(step["json"]);
            } else {
                thing->action(affordance)->invoke(step["json"])->wait();
            }
        } catch (const tw::PropertyError& error) {
            ASSERT_TRUE(error.validation()) << name << ": " << error.what();
            outcome = detail(*error.validation());
        } catch (const tw::ValidationError& error) {
            outcome = detail(error);
        }
        EXPECT_EQ(outcome, status == 422 ? response["text"].get<std::string>() : "accepted") << name;
        ++checked;
    }
    EXPECT_GE(checked, 40);
}

}  // namespace
