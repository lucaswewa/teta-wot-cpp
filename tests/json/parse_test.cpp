// The parser against what serde_json makes of request bodies
// (tests/http/wire_corpus.jsonl, written by conformance/reference-rs's
// `wire-corpus`): the same values, written back the same way, or the same
// error at the same line and column, and the same position in teta-wot's 422.

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>

#include "teta/wot/json.hpp"

namespace {

namespace json = teta::wot::json;

std::string from_hex(const std::string& hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
    }
    return out;
}

TEST(Parse, ReadsBodiesAsSerdeJson) {
    std::ifstream file(TETA_WOT_WIRE_CORPUS, std::ios::binary);
    ASSERT_TRUE(file) << TETA_WOT_WIRE_CORPUS;
    std::string line;
    int values = 0;
    int errors = 0;
    while (std::getline(file, line)) {
        const auto entry = json::Value::parse(line);
        if (!entry.contains("body")) {
            continue;  // a UUID case (tests/http)
        }
        const std::string body = from_hex(entry["body"].get<std::string>());
        const std::string shown = line.substr(0, 200);
        if (entry.contains("value")) {
            try {
                EXPECT_EQ(json::dump(json::parse(body)), entry["value"].get<std::string>()) << shown;
            } catch (const json::ParseError& error) {
                ADD_FAILURE() << shown << ": " << error.what();
            }
            ++values;
            continue;
        }
        try {
            const auto value = json::parse(body);
            ADD_FAILURE() << shown << ": parsed as " << json::dump(value);
        } catch (const json::ParseError& error) {
            EXPECT_EQ(error.message(), entry["error"].get<std::string>()) << shown;
            EXPECT_EQ(error.line(), entry["line"].get<std::size_t>()) << shown;
            EXPECT_EQ(error.column(), entry["column"].get<std::size_t>()) << shown;
            EXPECT_EQ(json::character_offset(body, error.line(), error.column()),
                      entry["position"].get<std::size_t>())
                << shown;
        }
        ++errors;
    }
    EXPECT_GT(values, 8000);
    EXPECT_GT(errors, 200);
}

TEST(Parse, KeepsNumberKinds) {
    EXPECT_TRUE(json::parse("1").is_number_unsigned());
    EXPECT_TRUE(json::parse("-1").is_number_integer());
    EXPECT_FALSE(json::parse("-1").is_number_unsigned());
    EXPECT_TRUE(json::parse("-0").is_number_float());
    EXPECT_TRUE(std::signbit(json::parse("-0").get<double>()));
    EXPECT_TRUE(json::parse("18446744073709551616").is_number_float());
    EXPECT_TRUE(json::parse("1.0").is_number_float());
}

TEST(Parse, RepeatedKeysKeepTheirPlace) {
    EXPECT_EQ(json::dump(json::parse(R"({"b":1,"a":2,"b":3})")), R"({"b":3,"a":2})");
}

TEST(Parse, ErrorsSayWhere) {
    try {
        json::parse("{\n  \"x\":\n}");
        FAIL();
    } catch (const json::ParseError& error) {
        EXPECT_STREQ(error.what(), "expected value at line 3 column 1");
    }
}

}  // namespace
