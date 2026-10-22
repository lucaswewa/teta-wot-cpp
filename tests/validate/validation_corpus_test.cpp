// The validator against teta-wot's own (tests/validate/validation_corpus.jsonl,
// written by conformance/reference-rs's `validation-corpus`): for every schema
// and input, the same coerced value or the same errors, byte for byte.

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <memory>
#include <string>

#include "teta/wot/validate.hpp"

namespace {

namespace tw = teta::wot;
using tw::json::Value;

std::string result_text(const tw::SchemaValidator& validator, const Value& input) {
    try {
        return tw::json::dump(Value{{"ok", validator.validate(input, {"body"})}});
    } catch (const tw::ValidationError& error) {
        Value issues = Value::array();
        for (const auto& issue : error.issues()) {
            issues.push_back(tw::to_json(issue));
        }
        return tw::json::dump(Value{{"errors", issues}});
    }
}

TEST(ValidationCorpus, EveryResultIsTetaWots) {
    std::ifstream file(TETA_WOT_VALIDATION_CORPUS, std::ios::binary);
    ASSERT_TRUE(file);
    std::map<int, std::unique_ptr<tw::SchemaValidator>> validators;
    std::map<int, std::string> names;
    std::string line;
    int checked = 0;
    int failed = 0;
    while (std::getline(file, line)) {
        const auto entry = Value::parse(line);
        const int id = entry["schema_id"].get<int>();
        if (entry.contains("schema")) {
            validators[id] =
                std::make_unique<tw::SchemaValidator>(tw::td::data_schema_from_json(entry["schema"]));
            names[id] = entry["name"].get<std::string>();
            continue;
        }
        const auto input_text = entry["input"].get<std::string>();
        const auto expected = entry["result"].get<std::string>();
        const auto actual = result_text(*validators.at(id), Value::parse(input_text));
        if (actual != expected && ++failed <= 20) {
            ADD_FAILURE() << names[id] << " with " << input_text << "\n  expected " << expected
                          << "\n  actual   " << actual;
        }
        ++checked;
    }
    EXPECT_EQ(failed, 0) << "of " << checked;
    EXPECT_GT(checked, 8000);
}

}  // namespace
