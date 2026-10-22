// teta-wot-core's tests/validate.rs, ported: coercion, schema validation and
// error reporting, case by case.

#include "teta/wot/validate.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace tw = teta::wot;
namespace coercion = tw::coercion;
using tw::Loc;
using tw::LocItem;
using tw::json::Value;

Value j(std::string_view text) {
    return Value::parse(text);
}

LocItem idx(std::size_t index) {
    return LocItem(index);
}

// serde's equality: an integer and a float are different values.
#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

tw::SchemaValidator validator(const Value& schema) {
    return tw::SchemaValidator(tw::td::data_schema_from_json(schema));
}

Value check(const Value& schema, const Value& input) {
    return validator(schema).validate(input, {"body"});
}

tw::ValidationError check_error(const Value& schema, const Value& input) {
    try {
        const auto value = check(schema, input);
        ADD_FAILURE() << "validated: " << tw::json::dump(value);
    } catch (const tw::ValidationError& error) {
        return error;
    }
    return tw::ValidationError({});
}

void assert_issue(const Value& schema, const Value& input, std::string_view kind, const Value& context) {
    const auto error = check_error(schema, input);
    ASSERT_EQ(error.issues().size(), 1u);
    const auto& issue = error.issues()[0];
    EXPECT_EQ(issue.type, kind);
    EXPECT_EQ(issue.loc, Loc{"body"});
    EXPECT_SAME(issue.input, input);
    EXPECT_SAME(issue.ctx.value_or(nullptr), context);
    EXPECT_FALSE(issue.msg.empty());
}

Value ok(const coercion::Result& result) {
    if (const auto* failure = std::get_if<coercion::Failure>(&result)) {
        ADD_FAILURE() << "failed: " << failure->type;
        return nullptr;
    }
    return std::get<Value>(result);
}

coercion::Failure err(const coercion::Result& result) {
    if (const auto* failure = std::get_if<coercion::Failure>(&result)) {
        return *failure;
    }
    ADD_FAILURE() << "coerced: " << tw::json::dump(std::get<Value>(result));
    return {"", ""};
}

std::string err_type(const coercion::Result& result) {
    return err(result).type;
}

constexpr auto u64_max = std::numeric_limits<std::uint64_t>::max();
constexpr auto i64_min = std::numeric_limits<std::int64_t>::min();

TEST(Validate, IntegerCoercionAcceptsWholeNumbersBooleansAndDigitStrings) {
    const std::vector<std::pair<Value, Value>> cases = {
        {42, 42},           {u64_max, u64_max},
        {i64_min, i64_min}, {2.0, 2},
        {-2.0, -2},         {true, 1},
        {false, 0},         {" +1_234.00 ", 1234},
        {"-002", -2},       {std::to_string(u64_max), u64_max},
    };
    for (const auto& [input, expected] : cases) {
        EXPECT_SAME(ok(coercion::integer(input)), expected) << tw::json::dump(input);
    }
}

TEST(Validate, IntegerCoercionDistinguishesParsingTypeFractionAndSizeErrors) {
    for (const char* input : {"", "+", "_1", "1_", "1__2", "2.", "2.01", "1e3", "0x10",
                              "\xEF\xBC\x91\xEF\xBC\x92", "18446744073709551616"}) {
        EXPECT_EQ(err_type(coercion::integer(input)), "int_parsing") << input;
    }
    for (const auto& input : {Value(nullptr), Value::array(), Value::object()}) {
        EXPECT_EQ(err(coercion::integer(input)),
                  (coercion::Failure{"int_type", "Input should be a valid integer"}));
    }
    EXPECT_EQ(err_type(coercion::integer(1.5)), "int_from_float");
    EXPECT_EQ(err_type(coercion::integer(1e20)), "int_parsing_size");
}

TEST(Validate, NumberCoercionReturnsFloatsAndAcceptsExponentsAndSeparators) {
    const std::vector<std::pair<Value, double>> cases = {
        {2, 2.0},        {2.5, 2.5},  {true, 1.0},   {false, 0.0}, {" -1_234.5 ", -1234.5},
        {"1e3", 1000.0}, {"2.", 2.0}, {".25", 0.25},
    };
    for (const auto& [input, expected] : cases) {
        const auto value = ok(coercion::number(input));
        EXPECT_SAME(value, Value(expected)) << tw::json::dump(input);
        EXPECT_TRUE(value.is_number_float());
    }
    for (const char* input : {"", "nope", "inf", "NaN", "1e999", "_1", "1_", "1__2", "1_.0", "1._0"}) {
        EXPECT_EQ(err_type(coercion::number(input)), "float_parsing") << input;
    }
    for (const auto& input : {Value(nullptr), Value::array(), Value::object()}) {
        EXPECT_EQ(err_type(coercion::number(input)), "float_type");
    }
}

std::string upper(std::string text) {
    for (auto& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

TEST(Validate, BooleanCoercionAcceptsDocumentedSpellingsAndZeroOrOne) {
    for (const char* spelling : {"0", "off", "f", "false", "n", "no"}) {
        EXPECT_SAME(ok(coercion::boolean(upper(spelling))), Value(false));
    }
    for (const char* spelling : {"1", "on", "t", "true", "y", "yes"}) {
        EXPECT_SAME(ok(coercion::boolean(upper(spelling))), Value(true));
    }
    const std::vector<std::pair<Value, bool>> cases = {
        {false, false}, {true, true}, {0, false}, {1, true}, {0.0, false}, {1.0, true},
    };
    for (const auto& [input, expected] : cases) {
        EXPECT_SAME(ok(coercion::boolean(input)), Value(expected));
    }
    for (const auto& input : {Value(2), Value(u64_max), Value("maybe"), Value(" true ")}) {
        EXPECT_EQ(err_type(coercion::boolean(input)), "bool_parsing");
    }
    for (const auto& input : {Value(2.0), Value(0.5), Value(nullptr), Value::array(), Value::object()}) {
        EXPECT_EQ(err_type(coercion::boolean(input)), "bool_type");
    }
}

TEST(Validate, StringCoercionPreservesStringsAndRejectsOtherJsonTypes) {
    for (const auto& input : {Value(""), Value(" caf\xC3\xA9 \xF0\x9F\xA6\x80 ")}) {
        EXPECT_SAME(ok(coercion::string(input)), input);
    }
    for (const auto& input : {Value(1), Value(true), Value(nullptr), Value::array(), Value::object()}) {
        EXPECT_EQ(err(coercion::string(input)),
                  (coercion::Failure{"string_type", "Input should be a valid string"}));
    }
}

TEST(Validate, PrimitiveSchemasApplyCoercionsAndLeaveInputUnchanged) {
    const std::vector<std::tuple<Value, Value, Value>> cases = {
        {j(R"({"type":"integer"})"), "2", 2},
        {j(R"({"type":"number"})"), "2", 2.0},
        {j(R"({"type":"boolean"})"), "YES", true},
        {j(R"({"type":"string"})"), "text", "text"},
        {j(R"({"type":"null"})"), nullptr, nullptr},
        {j("{}"), j(R"({"free": [1, null]})"), j(R"({"free": [1, null]})")},
    };
    for (const auto& [schema, input, expected] : cases) {
        const auto v = validator(schema);
        const auto original = input;
        EXPECT_SAME(v.validate(input, {}), expected);
        EXPECT_SAME(input, original);
    }
    assert_issue(j(R"({"type":"null"})"), 0, "none_required", nullptr);
}

TEST(Validate, NumericBoundsReportOriginalInputAndTypedContext) {
    const std::vector<std::tuple<const char*, int, const char*, const char*, const char*>> cases = {
        {"minimum", 2, "1", "greater_than_equal", "ge"},
        {"exclusiveMinimum", 2, "2", "greater_than", "gt"},
        {"maximum", 2, "3", "less_than_equal", "le"},
        {"exclusiveMaximum", 2, "2", "less_than", "lt"},
        {"multipleOf", 2, "3", "multiple_of", "multiple_of"},
    };
    for (const auto& [keyword, bound, input, kind, key] : cases) {
        for (const char* data_type : {"integer", "number"}) {
            Value schema = {{"type", data_type}};
            schema[keyword] = bound;
            const Value context_bound =
                std::string_view(data_type) == "number" ? Value(static_cast<double>(bound)) : Value(bound);
            assert_issue(schema, input, kind, Value{{key, context_bound}});
        }
    }
    const auto schema = j(R"({"type":"integer", "minimum":2, "maximum":4})");
    for (int input : {2, 4}) {
        EXPECT_SAME(check(schema, input), Value(input));
    }
    EXPECT_SAME(check(j(R"({"type":"number", "multipleOf":0.1})"), 0.3), Value(0.3));
}

TEST(Validate, NumericConstraintFailuresFollowDocumentedPriority) {
    auto schema = j(R"({"type":"integer", "multipleOf":2, "maximum":0, "exclusiveMaximum":0,
        "minimum":10, "exclusiveMinimum":10})");
    const std::vector<std::pair<std::optional<std::string>, std::string>> steps = {
        {std::nullopt, "multiple_of"}, {"multipleOf", "less_than_equal"},
        {"maximum", "less_than"},      {"exclusiveMaximum", "greater_than_equal"},
        {"minimum", "greater_than"},
    };
    for (const auto& [removed, kind] : steps) {
        if (removed) {
            schema.erase(*removed);
        }
        const auto error = check_error(schema, 3);
        ASSERT_EQ(error.issues().size(), 1u);
        EXPECT_EQ(error.issues()[0].type, kind);
    }
}

TEST(Validate, StringBoundsCountUnicodeCharactersAndCheckLengthBeforePattern) {
    const auto schema =
        j("{\"type\":\"string\", \"minLength\":2, \"maxLength\":3, "
          "\"pattern\":\"^[\xC3\xA9\xF0\x9F\xA6\x80]+$\"}");
    EXPECT_SAME(check(schema, "\xC3\xA9\xF0\x9F\xA6\x80"), Value("\xC3\xA9\xF0\x9F\xA6\x80"));
    assert_issue(schema, "x", "string_too_short", j(R"({"min_length":2})"));
    assert_issue(schema, "xxxx", "string_too_long", j(R"({"max_length":3})"));
    assert_issue(schema, "xx", "string_pattern_mismatch",
                 Value{{"pattern", "^[\xC3\xA9\xF0\x9F\xA6\x80]+$"}});
    EXPECT_SAME(check(j(R"({"type":"string", "pattern":"cat"})"), "a cat naps"), Value("a cat naps"));
}

TEST(Validate, InvalidPatternsAreRejectedInEverySupportedChildSchema) {
    for (const char* schema : {
             R"({"type":"string", "pattern":"["})",
             R"({"oneOf":[{"type":"string", "pattern":"["}]})",
             R"({"type":"array", "items":{"type":"string", "pattern":"["}})",
             R"({"type":"array", "items":[{"type":"string", "pattern":"["}]})",
             R"({"type":"object", "properties":{"name":{"type":"string", "pattern":"["}}})",
         }) {
        EXPECT_THROW(validator(j(schema)), tw::InvalidPattern) << schema;
    }
    const auto v = validator(j(R"({"type":"object", "properties":{
        "first":{"type":"string", "pattern":"^ok$"},
        "second":{"type":"array", "items":{"type":"string", "pattern":"^ok$"}}
    }})"));
    const auto clone = v;  // NOLINT(performance-unnecessary-copy-initialization): the copy is what's tested
    EXPECT_EQ(tw::json::dump(tw::td::to_json(clone.schema())), tw::json::dump(tw::td::to_json(v.schema())));
    try {
        clone.validate(j(R"({"first":"bad", "second":["bad"]})"), {});
        FAIL() << "validated";
    } catch (const tw::ValidationError& error) {
        ASSERT_EQ(error.issues().size(), 2u);
        EXPECT_EQ(error.issues()[0].loc, Loc{"first"});
        EXPECT_EQ(error.issues()[1].loc, (Loc{"second", idx(0)}));
    }
}

TEST(Validate, ArraysCoerceItemsAndReportAllIndexedErrors) {
    const auto schema = j(R"({"type":"array", "items":{"type":"integer"}})");
    EXPECT_SAME(check(schema, j(R"(["2", true, 3.0])")), j("[2, 1, 3]"));
    const auto error = check_error(schema, j(R"(["bad", 1, null])"));
    ASSERT_EQ(error.issues().size(), 2u);
    EXPECT_EQ(error.issues()[0].type, "int_parsing");
    EXPECT_EQ(error.issues()[0].loc, (Loc{"body", idx(0)}));
    EXPECT_EQ(error.issues()[1].type, "int_type");
    EXPECT_EQ(error.issues()[1].loc, (Loc{"body", idx(2)}));
    assert_issue(schema, Value::object(), "list_type", nullptr);
    EXPECT_SAME(check(j(R"({"type":"array"})"), j(R"([null, {"x":1}])")), j(R"([null, {"x":1}])"));
}

TEST(Validate, ArrayLengthErrorsHaveContextAndCorrectPriority) {
    const auto schema = j(R"({"type":"array", "items":{"type":"integer"}, "minItems":2, "maxItems":3})");
    assert_issue(schema, j(R"(["bad", 1, 2, 3])"), "too_long",
                 j(R"({"field_type":"List", "max_length":3, "actual_length":4})"));
    assert_issue(schema, j("[1]"), "too_short",
                 j(R"({"field_type":"List", "min_length":2, "actual_length":1})"));
    const auto error = check_error(schema, j(R"(["bad"])"));
    ASSERT_EQ(error.issues().size(), 1u);
    EXPECT_EQ(error.issues()[0].type, "int_parsing");
    EXPECT_SAME(check(schema, j("[1, 2]")), j("[1, 2]"));
    EXPECT_SAME(check(schema, j("[1, 2, 3]")), j("[1, 2, 3]"));
}

TEST(Validate, TuplesValidatePositionsAndReportMissingAndExcessItems) {
    const auto schema = j(R"({"type":"array", "items":[{"type":"integer"},{"type":"boolean"}]})");
    EXPECT_SAME(check(schema, j(R"(["2", "yes"])")), j("[2, true]"));
    const auto error = check_error(schema, j(R"(["bad"])"));
    ASSERT_EQ(error.issues().size(), 2u);
    EXPECT_EQ(error.issues()[0].type, "int_parsing");
    EXPECT_EQ(error.issues()[1].type, "missing");
    EXPECT_EQ(error.issues()[1].loc, (Loc{"body", idx(1)}));
    EXPECT_SAME(error.issues()[1].input, j(R"(["bad"])"));
    assert_issue(schema, j("[1, true, 3]"), "too_long",
                 j(R"({"field_type":"Tuple", "max_length":2, "actual_length":3})"));
    assert_issue(schema, false, "tuple_type", nullptr);
}

TEST(Validate, ModelsCoerceKnownFieldsIgnoreUnknownFieldsAndDoNotFillDefaults) {
    const auto schema = j(R"({"type":"object", "properties":{
        "count":{"type":"integer"}, "label":{"type":"string", "default":"untitled"}
    }, "required":["count"]})");
    EXPECT_SAME(check(schema, j(R"({"count":"2", "extra":99})")), j(R"({"count":2})"));
    const auto error = check_error(schema, Value::object());
    EXPECT_EQ(error.issues()[0].type, "missing");
    EXPECT_EQ(error.issues()[0].loc, (Loc{"body", "count"}));
    EXPECT_SAME(error.issues()[0].input, Value::object());
    assert_issue(schema, Value::array(), "model_attributes_type", nullptr);
    assert_issue(j(R"({"type":"object"})"), Value::array(), "dict_type", nullptr);
}

TEST(Validate, AdditionalPropertiesCanBeKeptForbiddenOrValidatedAsMapValues) {
    for (const char* schema : {R"({"type":"object"})", R"({"type":"object", "additionalProperties":true})"}) {
        EXPECT_SAME(check(j(schema), j(R"({"x":[1]})")), j(R"({"x":[1]})"));
    }
    EXPECT_SAME(
        check(j(R"({"type":"object", "properties":{}, "additionalProperties":true})"), j(R"({"x":1})")),
        j(R"({"x":1})"));
    auto error = check_error(j(R"({"type":"object", "properties":{"count":{"type":"integer"}},
        "required":["count"], "additionalProperties":false})"),
                             j(R"({"count":"bad", "extra":99})"));
    ASSERT_EQ(error.issues().size(), 2u);
    EXPECT_EQ(error.issues()[1].type, "extra_forbidden");
    EXPECT_EQ(error.issues()[1].loc, (Loc{"body", "extra"}));
    EXPECT_SAME(error.issues()[1].input, Value(99));
    const auto schema = j(R"({"type":"object", "additionalProperties":{"type":"integer"}})");
    EXPECT_SAME(check(schema, j(R"({"a":"2", "b":true})")), j(R"({"a":2, "b":1})"));
    error = check_error(schema, j(R"({"a":"bad", "b":null})"));
    ASSERT_EQ(error.issues().size(), 2u);
    EXPECT_EQ(error.issues()[0].loc, (Loc{"body", "a"}));
    EXPECT_EQ(error.issues()[1].loc, (Loc{"body", "b"}));
}

TEST(Validate, NestedModelsAndArraysPreserveErrorOrderAndFullLocation) {
    const auto schema = j(R"({"type":"object", "properties":{
        "rows":{"type":"array", "items":{"type":"object", "properties":{
            "count":{"type":"integer"}, "label":{"type":"string"}
        }, "required":["count","label"]}}, "enabled":{"type":"boolean"}
    }, "required":["rows","enabled"]})");
    const auto error =
        check_error(schema, j(R"({"rows":[{"count":"bad"}, {"count":1,"label":false}], "enabled":"maybe"})"));
    const std::vector<std::pair<std::string, Loc>> expected = {
        {"int_parsing", {"body", "rows", idx(0), "count"}},
        {"missing", {"body", "rows", idx(0), "label"}},
        {"string_type", {"body", "rows", idx(1), "label"}},
        {"bool_parsing", {"body", "enabled"}},
    };
    ASSERT_EQ(error.issues().size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(error.issues()[i].type, expected[i].first);
        EXPECT_EQ(error.issues()[i].loc, expected[i].second);
    }
}

TEST(Validate, NullableUnionAcceptsNullAndReportsNonNullErrorsWithoutBranchTags) {
    for (const char* branches :
         {R"([{"type":"integer"},{"type":"null"}])", R"([{"type":"null"},{"type":"integer"}])"}) {
        const Value schema = {{"oneOf", j(branches)}};
        EXPECT_SAME(check(schema, nullptr), Value(nullptr));
        EXPECT_SAME(check(schema, "2"), Value(2));
        assert_issue(schema, "bad", "int_parsing", nullptr);
    }
}

TEST(Validate, UnionsPreferMatchingJsonTypesBeforeCoercionAndDiscardFailedAttempts) {
    auto schema = j(R"({"oneOf":[{"type":"integer"},{"type":"string"}]})");
    EXPECT_SAME(check(schema, "2"), Value("2"));
    EXPECT_SAME(check(schema, 2), Value(2));
    EXPECT_SAME(check(schema, true), Value(1));
    schema = j(R"({"oneOf":[{"type":"integer","minimum":10},{"type":"number"}]})");
    EXPECT_SAME(check(schema, 2), Value(2.0));
}

TEST(Validate, FailedUnionsReportEachBranchWithItsTagInOrder) {
    const auto schema = j(R"({"oneOf":[{"type":"integer"},{"type":"number"},{"type":"string"},
        {"type":"boolean"},{"type":"null"},{"type":"array"},
        {"type":"object","title":"Model","properties":{"field":{"type":"integer"}},"required":["field"]},
        {"type":"object","properties":{"field":{"type":"integer"}},"required":["field"]},
        {"enum":["never"]}]})");
    const auto error = check_error(schema, Value::object());
    const std::vector<std::pair<const char*, const char*>> expected = {
        {"int", "int_type"},   {"float", "float_type"},   {"str", "string_type"},
        {"bool", "bool_type"}, {"none", "none_required"}, {"list", "list_type"},
        {"Model", "missing"},  {"dict", "missing"},       {"branch-8", "literal_error"},
    };
    ASSERT_EQ(error.issues().size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& [tag, kind] = expected[i];
        Loc loc = {"body", tag};
        if (std::string_view(kind) == "missing") {
            loc.emplace_back("field");
        }
        EXPECT_EQ(error.issues()[i].type, kind);
        EXPECT_EQ(error.issues()[i].loc, loc);
        EXPECT_SAME(error.issues()[i].input, Value::object());
    }
}

TEST(Validate, EnumConstantAndLiteralUnionsUsePythonNumericEquality) {
    for (const char* schema :
         {R"({"enum":[1,2]})", R"({"const":1})", R"({"oneOf":[{"const":1},{"const":2}]})"}) {
        EXPECT_SAME(check(j(schema), true), Value(1));
        EXPECT_SAME(check(j(schema), 1.0), Value(1));
        EXPECT_EQ(check_error(j(schema), "1").issues()[0].type, "literal_error");
    }
    assert_issue(j(R"({"enum":["a","b","c"]})"), "d", "literal_error",
                 j(R"({"expected":"'a', 'b' or 'c'"})"));
    assert_issue(j(R"({"enum":[true,false,null]})"), "no", "literal_error",
                 j(R"({"expected":"True, False or None"})"));
    assert_issue(j(R"({"const":"fixed"})"), "other", "literal_error", j(R"({"expected":"'fixed'"})"));
}

TEST(Validate, ValidationErrorsSerializeAndFormatLocationsAndContext) {
    const auto error = tw::ValidationError::missing({"body", "rows", idx(2)}, nullptr);
    Value issues = Value::array();
    for (const auto& issue : error.issues()) {
        issues.push_back(tw::to_json(issue));
    }
    EXPECT_SAME(issues,
                j(R"([{"type":"missing", "loc":["body","rows",2], "msg":"Field required", "input":null}])"));
    EXPECT_STREQ(error.what(), "1 validation error\n  body.rows.2: Field required [type=missing]");
    EXPECT_EQ(LocItem("field").to_string(), "field");
    EXPECT_EQ(idx(2).to_string(), "2");
    const auto root = tw::ValidationError::missing({}, Value::object());
    EXPECT_STREQ(root.what(), "1 validation error\n  Field required [type=missing]");
    const tw::ValidationError combined({error.issues()[0], root.issues()[0]});
    EXPECT_TRUE(std::string_view(combined.what()).starts_with("2 validation errors\n"));
    const auto bound = check_error(j(R"({"type":"integer", "minimum":2})"), 1);
    EXPECT_SAME(tw::to_json(bound.issues()[0])["ctx"], j(R"({"ge":2})"));
}

// teta-wot's `from_serde` is `decode_error`; the message is the decoder's.
TEST(Validate, ValueErrorsPreserveInputAndLocation) {
    const Value input = "not an integer";
    const auto error = tw::ValidationError::decode_error({"body"}, "invalid type: string", input);
    ASSERT_EQ(error.issues().size(), 1u);
    EXPECT_EQ(error.issues()[0].type, "value_error");
    EXPECT_EQ(error.issues()[0].loc, Loc{"body"});
    EXPECT_SAME(error.issues()[0].input, input);
    EXPECT_EQ(error.issues()[0].msg, "Value error, invalid type: string");
    EXPECT_EQ(error.issues()[0].ctx, std::nullopt);
    EXPECT_EQ(tw::ValidationError::single(error.issues()[0]).issues(), error.issues());
}

}  // namespace
