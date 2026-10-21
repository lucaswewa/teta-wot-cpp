// teta-wot-td's unit tests, ported: the TD model, its builders and checks,
// constraints and the conversion's edge cases.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "teta/wot/td/constraints.hpp"
#include "teta/wot/td/convert.hpp"
#include "teta/wot/td/describe.hpp"
#include "teta/wot/td/thing.hpp"

namespace {

namespace td = teta::wot::td;
using td::DataSchema;
using td::DataType;
using td::Form;
using td::Operation;
using teta::wot::json::Value;

// Equal as JSON values, whatever the key order (as serde_json's `Value`
// compares maps).
::testing::AssertionResult same(const Value& actual, const Value& expected) {
    if (nlohmann::json::parse(actual.dump()) == nlohmann::json::parse(expected.dump())) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << actual.dump() << "\n    is not\n" << expected.dump();
}

template <typename F>
td::TdError::Kind td_error(F build) {
    try {
        build();
    } catch (const td::TdError& error) {
        return error.kind();
    }
    ADD_FAILURE() << "no TdError";
    return td::TdError::Kind::invalid_context;
}

template <typename F>
td::ConstraintError::Kind constraint_error(F apply) {
    try {
        apply();
    } catch (const td::ConstraintError& error) {
        return error.kind();
    }
    ADD_FAILURE() << "no ConstraintError";
    return td::ConstraintError::Kind::untyped;
}

template <typename F>
td::ConversionError::Kind conversion_error(F convert) {
    try {
        convert();
    } catch (const td::ConversionError& error) {
        return error.kind();
    }
    ADD_FAILURE() << "no ConversionError";
    return td::ConversionError::Kind::conflict;
}

// ---- Conversion ---------------------------------------------------------------

TEST(Convert, NullConstSelectsOnlyTheNullType) {
    const auto schema = td::from_json_schema({{"type", {"string", "null"}}, {"const", nullptr}});
    EXPECT_EQ(schema.data_type, DataType::null);
    EXPECT_FALSE(schema.one_of);
    EXPECT_TRUE(same(td::to_json(schema), {{"type", "null"}}));
}

TEST(Convert, NullConstConflictingWithANonNullTypeIsRejected) {
    EXPECT_EQ(conversion_error([] { td::from_json_schema({{"type", "string"}, {"const", nullptr}}); }),
              td::ConversionError::Kind::conflict);
}

TEST(Convert, NumberSubsumesIntegerInATypeUnion) {
    const auto schema = td::from_json_schema({{"type", {"integer", "number"}}, {"minimum", 0}});
    EXPECT_EQ(schema.data_type, DataType::number);
    EXPECT_TRUE(same(td::to_json(schema), {{"type", "number"}, {"minimum", 0}}));
}

TEST(Convert, LocalRefsInlineAndSiblingTermsOverrideThem) {
    const auto schema = td::from_json_schema(
        {{"$defs", {{"Reading", {{"type", "number"}, {"description", "Base description"}}}}},
         {"$ref", "#/$defs/Reading"},
         {"description", "Field description"}});
    EXPECT_TRUE(same(td::to_json(schema), {{"type", "number"}, {"description", "Field description"}}));
}

TEST(Convert, TupleItemsArePreservedAndFalseItemsIsRemoved) {
    const auto schema = td::from_json_schema({{"type", "array"},
                                              {"prefixItems", {{{"type", "string"}}, {{"type", "integer"}}}},
                                              {"items", false},
                                              {"maxItems", 2}});
    EXPECT_TRUE(
        same(td::to_json(schema),
             {{"type", "array"}, {"items", {{{"type", "string"}}, {{"type", "integer"}}}}, {"maxItems", 2}}));
}

TEST(Convert, RecursiveRefsStopAtTheConfiguredLimit) {
    const Value schema = {
        {"$defs", {{"Node", {{"type", "object"}, {"properties", {{"next", {{"$ref", "#/$defs/Node"}}}}}}}}},
        {"$ref", "#/$defs/Node"}};
    EXPECT_EQ(conversion_error([&] { td::from_json_schema(schema, {.recursion_limit = 4}); }),
              td::ConversionError::Kind::recursion_limit);
}

TEST(Convert, NestedSingleAllOfCountsTowardTheRecursionLimit) {
    const Value schema = {{"allOf", {{{"allOf", {{{"allOf", {{{"type", "string"}}}}}}}}}}};
    EXPECT_EQ(conversion_error([&] { td::from_json_schema(schema, {.recursion_limit = 2}); }),
              td::ConversionError::Kind::recursion_limit);
}

TEST(Convert, OtherErrors) {
    EXPECT_EQ(conversion_error([] { td::from_json_schema({{"$ref", "https://example.test/schema"}}); }),
              td::ConversionError::Kind::non_local_reference);
    EXPECT_EQ(conversion_error([] { td::from_json_schema({{"$ref", "#/$defs/Missing"}}); }),
              td::ConversionError::Kind::unresolved_reference);
    EXPECT_EQ(conversion_error(
                  [] { td::from_json_schema({{"anyOf", Value::array()}, {"oneOf", Value::array()}}); }),
              td::ConversionError::Kind::conflict);
    EXPECT_EQ(conversion_error([] { td::from_json_schema(false); }), td::ConversionError::Kind::false_schema);
    EXPECT_EQ(conversion_error([] { td::from_json_schema(42); }), td::ConversionError::Kind::not_a_schema);
    EXPECT_EQ(conversion_error([] { td::from_json_schema({{"$dynamicRef", "#x"}}); }),
              td::ConversionError::Kind::unsupported);
    EXPECT_EQ(conversion_error([] { td::from_json_schema({{"type", "integer"}, {"minimum", "low"}}); }),
              td::ConversionError::Kind::invalid_data_schema);
}

TEST(Convert, EnumValuesGoToTheBranchOfTheirType) {
    const auto schema =
        td::from_json_schema({{"type", {"string", "integer", "null"}}, {"enum", {"a", 1, nullptr}}});
    EXPECT_TRUE(same(td::to_json(schema), {{"oneOf",
                                            {{{"type", "string"}, {"enum", {"a"}}},
                                             {{"type", "integer"}, {"enum", {1}}},
                                             {{"type", "null"}}}}}));
}

// ---- Constraints ----------------------------------------------------------------

TEST(Constraints, NumericConstraintsWriteTheExpectedSchemaTerms) {
    auto schema = DataSchema::of(DataType::integer);
    td::Constraints{}.gt(-2).ge(-1).lt(10).le(9).multiple_of(2).apply(schema);
    EXPECT_TRUE(same(td::to_json(schema), {{"type", "integer"},
                                           {"exclusiveMinimum", -2},
                                           {"minimum", -1},
                                           {"exclusiveMaximum", 10},
                                           {"maximum", 9},
                                           {"multipleOf", 2}}));
    auto number = DataSchema::of(DataType::number);
    td::Constraints{}.multiple_of(0.5).allow_inf_nan(false).apply(number);
    EXPECT_TRUE(same(td::to_json(number), {{"type", "number"}, {"multipleOf", 0.5}}));
}

TEST(Constraints, LengthsFollowTheSchemaType) {
    const std::pair<DataType, Value> cases[] = {
        {DataType::string, {{"type", "string"}, {"minLength", 2}, {"maxLength", 5}}},
        {DataType::array, {{"type", "array"}, {"minItems", 2}, {"maxItems", 5}}},
        {DataType::object, {{"type", "object"}, {"minProperties", 2}, {"maxProperties", 5}}},
    };
    for (const auto& [type, expected] : cases) {
        auto schema = DataSchema::of(type);
        td::Constraints{}.min_length(2).max_length(5).apply(schema);
        EXPECT_TRUE(same(td::to_json(schema), expected));
    }
    auto string = DataSchema::of(DataType::string);
    td::Constraints{}.pattern("^[A-Z]+$").apply(string);
    EXPECT_TRUE(same(td::to_json(string), {{"type", "string"}, {"pattern", "^[A-Z]+$"}}));
}

TEST(Constraints, OneOfConstraintsApplyOnlyToSuitableBranches) {
    DataSchema schema;
    schema.one_of = {DataSchema::of(DataType::integer), DataSchema::of(DataType::string),
                     DataSchema::of(DataType::null)};
    td::Constraints{}.ge(1).min_length(2).pattern("^x").apply(schema);
    EXPECT_TRUE(same(td::to_json(schema), {{"oneOf",
                                            {{{"type", "integer"}, {"minimum", 1}},
                                             {{"type", "string"}, {"minLength", 2}, {"pattern", "^x"}},
                                             {{"type", "null"}}}}}));
}

TEST(Constraints, EmptyConstraintsLeaveEvenAnUntypedSchemaUnchanged) {
    td::Constraints constraints;
    EXPECT_TRUE(constraints.empty());
    auto schema = DataSchema::any();
    constraints.apply(schema);
    EXPECT_TRUE(same(td::to_json(schema), Value::object()));
    EXPECT_FALSE(td::Constraints{}.ge(1).empty());
}

TEST(Constraints, LargeUnsignedBoundsAreNotTruncated) {
    auto schema = DataSchema::of(DataType::integer);
    td::Constraints{}.le(std::numeric_limits<std::uint64_t>::max()).apply(schema);
    EXPECT_EQ(td::to_json(schema)["maximum"].get<std::uint64_t>(), std::numeric_limits<std::uint64_t>::max());
    EXPECT_TRUE(td::Bound(std::uint64_t{7}) == td::Bound(std::int64_t{7}));
}

TEST(Constraints, InvalidConstraintsReportSpecificErrors) {
    using K = td::ConstraintError::Kind;
    auto integer = DataSchema::of(DataType::integer);
    EXPECT_EQ(constraint_error([&] { td::Constraints{}.pattern("x").apply(integer); }), K::unsuitable);
    auto untyped = DataSchema::any();
    EXPECT_EQ(constraint_error([&] { td::Constraints{}.ge(1).apply(untyped); }), K::untyped);
    DataSchema string_or_null;
    string_or_null.one_of = {DataSchema::of(DataType::string), DataSchema::of(DataType::null)};
    EXPECT_EQ(constraint_error([&] { td::Constraints{}.gt(0).apply(string_or_null); }), K::unsuitable);
    for (const td::Bound invalid : {td::Bound(0), td::Bound(-1), td::Bound(0.0)}) {
        auto number = DataSchema::of(DataType::number);
        EXPECT_EQ(constraint_error([&] { td::Constraints{}.multiple_of(invalid).apply(number); }),
                  K::not_positive);
    }
    auto number = DataSchema::of(DataType::number);
    EXPECT_EQ(constraint_error([&] { td::Constraints{}.gt(INFINITY).apply(number); }), K::not_finite);
    EXPECT_EQ(constraint_error([&] { td::Constraints{}.multiple_of(NAN).apply(number); }), K::not_finite);
}

TEST(Constraints, AnInvalidBoundDoesNotPartiallyModifyASchema) {
    auto schema = DataSchema::of(DataType::number);
    const auto original = schema;
    EXPECT_THROW(td::Constraints{}.ge(1).le(INFINITY).apply(schema), td::ConstraintError);
    EXPECT_EQ(schema, original);

    DataSchema union_schema;
    union_schema.one_of = {DataSchema::of(DataType::string), DataSchema::of(DataType::number)};
    const auto original_union = union_schema;
    EXPECT_THROW(td::Constraints{}.min_length(2).le(INFINITY).apply(union_schema), td::ConstraintError);
    EXPECT_EQ(union_schema, original_union);
}

// ---- DataSchema -------------------------------------------------------------------

TEST(DataSchema, TypesUseTdNamesAndTypeArraysAreRejected) {
    for (const auto* name : {"boolean", "integer", "number", "string", "object", "array", "null"}) {
        const auto type = td::data_type_from_string(name);
        ASSERT_TRUE(type);
        EXPECT_STREQ(td::to_string(*type), name);
    }
    EXPECT_THROW(td::data_schema_from_json({{"type", {"integer", "null"}}}), td::InvalidDataSchema);
}

TEST(DataSchema, BuildersWriteTypedTermsAndGrowSemanticAnnotations) {
    EXPECT_TRUE(same(td::to_json(DataSchema::any()), Value::object()));
    auto single = DataSchema::of(DataType::number);
    single.with_semantic_type("saref:Temperature");
    EXPECT_TRUE(same(td::to_json(single), {{"type", "number"}, {"@type", "saref:Temperature"}}));
    auto schema = DataSchema::of(DataType::number);
    schema.with_title("Temperature")
        .with_description("Current reading")
        .with_unit("degree Celsius")
        .with_default(21.5)
        .with_semantic_type("saref:Temperature")
        .with_semantic_type("ex:Reading");
    EXPECT_TRUE(same(td::to_json(schema), {{"type", "number"},
                                           {"title", "Temperature"},
                                           {"description", "Current reading"},
                                           {"unit", "degree Celsius"},
                                           {"default", 21.5},
                                           {"@type", {"saref:Temperature", "ex:Reading"}}}));
}

TEST(DataSchema, NullDefaultAndConstAreOmitted) {
    auto schema = DataSchema::any();
    schema.with_default(nullptr);
    schema.constant = nullptr;
    EXPECT_TRUE(same(td::to_json(schema), Value::object()));
    const auto decoded = td::data_schema_from_json({{"default", nullptr}, {"const", nullptr}});
    EXPECT_FALSE(decoded.default_value);
    EXPECT_FALSE(decoded.constant);
}

TEST(DataSchema, NestedObjectSchemaRoundTripsWithExtensionTerms) {
    const Value document = {
        {"type", "object"},
        {"@type", "ex:Measurement"},
        {"titles", {{"en", "Measurement"}, {"fr", "Mesure"}}},
        {"descriptions", {{"en", "A sensor reading"}}},
        {"readOnly", true},
        {"properties",
         {{"reading",
           {{"oneOf", {{{"type", "number"}, {"minimum", -40}, {"maximum", 125}}, {{"type", "null"}}}}}},
          {"status", {{"type", "string"}, {"enum", {"ok", "warning"}}}}}},
        {"required", {"reading"}},
        {"additionalProperties", false},
        {"ex:quality", "calibrated"}};
    const auto schema = td::data_schema_from_json(document);
    EXPECT_EQ(schema.data_type, DataType::object);
    EXPECT_EQ(schema.extra["additionalProperties"], false);
    EXPECT_EQ(schema.extra["ex:quality"], "calibrated");
    EXPECT_TRUE(same(td::to_json(schema), document));
}

TEST(DataSchema, ArrayItemsSupportSingleAndTupleSchemas) {
    const Value single = {{"type", "array"},
                          {"items", {{"type", "integer"}, {"minimum", 0}}},
                          {"minItems", 1},
                          {"maxItems", 3}};
    const auto a = td::data_schema_from_json(single);
    EXPECT_FALSE(a.items->tuple);
    EXPECT_TRUE(same(td::to_json(a), single));
    const Value tuple = {
        {"type", "array"},
        {"items", {{{"type", "string"}, {"minLength", 1}}, {{"type", "number"}, {"multipleOf", 0.5}}}}};
    const auto b = td::data_schema_from_json(tuple);
    EXPECT_TRUE(b.items->tuple);
    EXPECT_TRUE(same(td::to_json(b), tuple));
}

TEST(DataSchema, StringConstraintsKeepTheirJsonNames) {
    const Value document = {{"type", "string"},
                            {"minLength", 2},
                            {"maxLength", 10},
                            {"pattern", "^[A-Z]+$"},
                            {"format", "uri"},
                            {"contentEncoding", "base64"},
                            {"contentMediaType", "text/plain"},
                            {"const", "AB"}};
    EXPECT_TRUE(same(td::to_json(td::data_schema_from_json(document)), document));
}

// ---- Forms, links, @context --------------------------------------------------------

TEST(Form, OperationsHaveTheExpectedNamesAndScopes) {
    using S = td::OperationScope;
    const std::tuple<Operation, const char*, S> cases[] = {
        {Operation::readproperty, "readproperty", S::property},
        {Operation::writeproperty, "writeproperty", S::property},
        {Operation::observeproperty, "observeproperty", S::property},
        {Operation::unobserveproperty, "unobserveproperty", S::property},
        {Operation::invokeaction, "invokeaction", S::action},
        {Operation::queryaction, "queryaction", S::action},
        {Operation::cancelaction, "cancelaction", S::action},
        {Operation::subscribeevent, "subscribeevent", S::event},
        {Operation::unsubscribeevent, "unsubscribeevent", S::event},
        {Operation::readallproperties, "readallproperties", S::thing},
        {Operation::writeallproperties, "writeallproperties", S::thing},
        {Operation::readmultipleproperties, "readmultipleproperties", S::thing},
        {Operation::writemultipleproperties, "writemultipleproperties", S::thing},
        {Operation::observeallproperties, "observeallproperties", S::thing},
        {Operation::unobserveallproperties, "unobserveallproperties", S::thing},
        {Operation::queryallactions, "queryallactions", S::thing},
        {Operation::subscribeallevents, "subscribeallevents", S::thing},
        {Operation::unsubscribeallevents, "unsubscribeallevents", S::thing},
    };
    for (const auto& [op, name, scope] : cases) {
        EXPECT_EQ(td::scope(op), scope) << name;
        EXPECT_STREQ(td::to_string(op), name);
        EXPECT_EQ(td::form_from_json({{"href", "/x"}, {"op", name}}).operations(),
                  std::vector<Operation>{op});
    }
}

TEST(Form, BuilderOmitsUnsetFieldsAndPreservesOperationShape) {
    const Form bare("/temperature");
    EXPECT_TRUE(bare.operations().empty());
    EXPECT_TRUE(same(td::to_json(bare), {{"href", "/temperature"}}));
    Form single("/temperature");
    single.with_op(td::OneOrMany<Operation>(Operation::readproperty));
    EXPECT_TRUE(same(td::to_json(single), {{"href", "/temperature"}, {"op", "readproperty"}}));
    Form multiple("/temperature");
    multiple.with_op({Operation::readproperty, Operation::observeproperty})
        .with_content_type("application/json")
        .with_subprotocol("sse")
        .with_term("htv:methodName", "GET");
    EXPECT_TRUE(same(td::to_json(multiple), {{"href", "/temperature"},
                                             {"op", {"readproperty", "observeproperty"}},
                                             {"contentType", "application/json"},
                                             {"subprotocol", "sse"},
                                             {"htv:methodName", "GET"}}));
}

TEST(Form, RoundTripsResponsesSecurityAndExtensionTerms) {
    const Value document = {{"href", "https://example.test/actions/reset"},
                            {"op", {"invokeaction", "queryaction"}},
                            {"contentType", "application/json"},
                            {"contentCoding", "gzip"},
                            {"subprotocol", "longpoll"},
                            {"security", {"basic_sc", "bearer_sc"}},
                            {"scopes", "admin"},
                            {"response", {{"contentType", "text/plain"}, {"x-response-term", 7}}},
                            {"additionalResponses",
                             {{{"success", false},
                               {"contentType", "application/problem+json"},
                               {"schema", "problem"},
                               {"x-error-term", true}}}},
                            {"htv:methodName", "POST"}};
    const auto form = td::form_from_json(document);
    EXPECT_EQ(form.operations(), (std::vector<Operation>{Operation::invokeaction, Operation::queryaction}));
    EXPECT_EQ(form.response->extra["x-response-term"], 7);
    EXPECT_EQ((*form.additional_responses)[0].extra["x-error-term"], true);
    EXPECT_EQ(form.extra["htv:methodName"], "POST");
    EXPECT_TRUE(same(td::to_json(form), document));
}

TEST(Form, RejectsMissingHrefAndUnknownOperation) {
    EXPECT_THROW(td::form_from_json({{"op", "readproperty"}}), td::TdParseError);
    EXPECT_THROW(td::form_from_json({{"href", "/x"}, {"op", "notanoperation"}}), td::TdParseError);
}

TEST(Link, BuilderAndRoundTrip) {
    EXPECT_TRUE(
        same(td::to_json(td::Link("https://example.test/thing")), {{"href", "https://example.test/thing"}}));
    td::Link link("/alternate");
    link.with_rel("alternate").with_media_type("application/td+json");
    EXPECT_TRUE(same(td::to_json(link),
                     {{"href", "/alternate"}, {"rel", "alternate"}, {"type", "application/td+json"}}));
    const Value document = {
        {"href", "https://example.test/icon.png"}, {"type", "image/png"}, {"rel", "icon"},
        {"anchor", "https://example.test/thing"},  {"sizes", "48x48"},    {"hreflang", {"en", "fr"}},
        {"x-link-term", {{"priority", 1}}}};
    const auto decoded = td::link_from_json(document);
    EXPECT_TRUE(decoded.hreflang->is_many());
    EXPECT_TRUE(same(td::to_json(decoded), document));
    const Value single_language = {{"href", "/help"}, {"hreflang", "en"}};
    EXPECT_FALSE(td::link_from_json(single_language).hreflang->is_many());
    EXPECT_TRUE(same(td::to_json(td::link_from_json(single_language)), single_language));
    EXPECT_THROW(td::link_from_json({{"rel", "alternate"}}), td::TdParseError);
}

TEST(Context, DefaultIsTheTd11Uri) {
    const td::Context context;
    EXPECT_EQ(td::to_json(context), td::td_context_v1_1);
    EXPECT_NO_THROW(context.validate());
}

TEST(Context, AddingEntriesKeepsTheBaseUriAndCombinesPrefixes) {
    td::Context context;
    context.push(td::ContextEntry::of_uri("https://example.test/context"));
    context.add_prefix("saref", "https://saref.etsi.org/core/");
    context.add_prefix("ex", "https://example.test/vocab#");
    const Value expected = {
        td::td_context_v1_1,
        "https://example.test/context",
        {{"saref", "https://saref.etsi.org/core/"}, {"ex", "https://example.test/vocab#"}}};
    EXPECT_EQ(td::to_json(context), expected);
    EXPECT_NO_THROW(context.validate());
    EXPECT_EQ(td::context_from_json(expected), context);
}

TEST(Context, Td10UriIsValidOnlyImmediatelyBeforeTd11) {
    const Value valid = {
        td::td_context_v1, td::td_context_v1_1, {{"@language", "en"}, {"ex", "https://example.test/vocab#"}}};
    EXPECT_NO_THROW(td::context_from_json(valid).validate());
    EXPECT_EQ(td::to_json(td::context_from_json(valid)), valid);
    const Value invalid[] = {
        td::td_context_v1,
        Value::array({td::td_context_v1}),
        {td::td_context_v1, "https://example.test/context", td::td_context_v1_1},
        {td::td_context_v1_1, td::td_context_v1},
        {td::td_context_v1, td::td_context_v1_1, td::td_context_v1},
        Value::array(),
        "https://example.test/context",
        {"https://example.test/context", td::td_context_v1_1},
        {{{"ex", "https://example.test/vocab#"}}, td::td_context_v1_1},
    };
    for (const auto& document : invalid) {
        EXPECT_EQ(td_error([&] { td::context_from_json(document).validate(); }),
                  td::TdError::Kind::invalid_context)
            << document.dump();
    }
}

// ---- Affordances ----------------------------------------------------------------------

TEST(Affordance, PropertyBuilderFlattensSchemaTermsAndKeepsExtensionTermsLast) {
    auto schema = DataSchema::of(DataType::integer);
    schema.with_title("Old title").with_description("Old description");
    schema.extra["ex:source"] = "sensor";
    auto builder = td::PropertyBuilder(schema);
    builder.title("Temperature")
        .description("Current reading")
        .read_only(true)
        .write_only(false)
        .observable(true)
        .default_value(21)
        .unit("degree Celsius")
        .semantic_type("saref:Temperature")
        .semantic_type("ex:Reading")
        .form(Form("/temperature").with_op(td::OneOrMany<Operation>(Operation::readproperty)))
        .uri_variable("id", DataSchema::of(DataType::string));
    const auto property = builder.build();
    const Value expected = {{"type", "integer"},
                            {"title", "Temperature"},
                            {"description", "Current reading"},
                            {"readOnly", true},
                            {"writeOnly", false},
                            {"observable", true},
                            {"default", 21},
                            {"unit", "degree Celsius"},
                            {"@type", {"saref:Temperature", "ex:Reading"}},
                            {"forms", {{{"href", "/temperature"}, {"op", "readproperty"}}}},
                            {"uriVariables", {{"id", {{"type", "string"}}}}},
                            {"ex:source", "sensor"}};
    EXPECT_TRUE(same(td::to_json(property), expected));
    EXPECT_EQ(td::property_from_json(expected), property);
    const auto text = td::to_json(property).dump();
    EXPECT_LT(text.find("\"readOnly\""), text.find("\"forms\""));
    EXPECT_LT(text.find("\"forms\""), text.find("\"uriVariables\""));
    EXPECT_LT(text.find("\"uriVariables\""), text.find("\"observable\""));
    EXPECT_LT(text.find("\"observable\""), text.find("\"ex:source\""));
}

TEST(Affordance, PropertyBuilderWritesAccessFlagsAndOmitsNullDefault) {
    const auto property = td::PropertyBuilder(DataSchema::of(DataType::null))
                              .default_value(nullptr)
                              .form(Form("/empty"))
                              .build();
    EXPECT_TRUE(same(
        td::to_json(property),
        {{"type", "null"}, {"readOnly", false}, {"writeOnly", false}, {"forms", {{{"href", "/empty"}}}}}));
}

TEST(Affordance, ActionBuilderRoundTripsInputOutputAndFlags) {
    const auto action = td::ActionBuilder()
                            .title("Reset")
                            .description("Reset the device")
                            .input(DataSchema::of(DataType::integer))
                            .output(DataSchema::of(DataType::boolean))
                            .safe(false)
                            .idempotent(true)
                            .synchronous(false)
                            .semantic_type("ex:ResetAction")
                            .form(Form("/reset").with_op(td::OneOrMany<Operation>(Operation::invokeaction)))
                            .uri_variable("id", DataSchema::of(DataType::string))
                            .build();
    const Value expected = {{"title", "Reset"},
                            {"description", "Reset the device"},
                            {"input", {{"type", "integer"}}},
                            {"output", {{"type", "boolean"}}},
                            {"safe", false},
                            {"idempotent", true},
                            {"synchronous", false},
                            {"@type", "ex:ResetAction"},
                            {"forms", {{{"href", "/reset"}, {"op", "invokeaction"}}}},
                            {"uriVariables", {{"id", {{"type", "string"}}}}}};
    EXPECT_TRUE(same(td::to_json(action), expected));
    EXPECT_EQ(td::action_from_json(expected), action);
}

TEST(Affordance, EventBuilderAndReaderPreserveEventDataTerms) {
    const auto event = td::EventBuilder()
                           .title("Alarm")
                           .description("Raised when a threshold is crossed")
                           .subscription(DataSchema::of(DataType::string))
                           .data(DataSchema::of(DataType::number))
                           .cancellation(DataSchema::of(DataType::boolean))
                           .semantic_type("ex:AlarmEvent")
                           .semantic_type("ex:Notification")
                           .form(Form("/alarm").with_op(td::OneOrMany<Operation>(Operation::subscribeevent)))
                           .build();
    const Value expected = {{"title", "Alarm"},
                            {"description", "Raised when a threshold is crossed"},
                            {"subscription", {{"type", "string"}}},
                            {"data", {{"type", "number"}}},
                            {"cancellation", {{"type", "boolean"}}},
                            {"@type", {"ex:AlarmEvent", "ex:Notification"}},
                            {"forms", {{{"href", "/alarm"}, {"op", "subscribeevent"}}}}};
    EXPECT_TRUE(same(td::to_json(event), expected));
    EXPECT_EQ(td::event_from_json(expected), event);
    const Value full = {{"forms", {{{"href", "/alarm"}}}},
                        {"dataResponse", {{"type", "object"}}},
                        {"uriVariables", {{"id", {{"type", "integer"}}}}},
                        {"titles", {{"en", "Alarm"}, {"fr", "Alarme"}}},
                        {"ex:severity", "high"}};
    const auto decoded = td::event_from_json(full);
    EXPECT_EQ(decoded.extra["ex:severity"], "high");
    EXPECT_TRUE(same(td::to_json(decoded), full));
}

TEST(Affordance, ReadingRequiresForms) {
    EXPECT_THROW(td::property_from_json({{"type", "string"}}), td::TdParseError);
    EXPECT_THROW(td::action_from_json({{"title", "Reset"}}), td::TdParseError);
    EXPECT_THROW(td::event_from_json({{"title", "Alarm"}}), td::TdParseError);
}

// ---- Thing Descriptions -------------------------------------------------------------

TEST(Thing, MinimalBuilderAddsNoSecurityAndWritesRequiredFields) {
    const auto td = td::ThingBuilder("Example Thing").build();
    EXPECT_STREQ(td::no_security_name, "no_security");
    EXPECT_EQ(td::to_json(td).dump(),
              Value({{"title", "Example Thing"},
                     {"properties", Value::object()},
                     {"actions", Value::object()},
                     {"securityDefinitions",
                      {{"no_security", {{"description", "No security"}, {"scheme", "nosec"}}}}},
                     {"security", "no_security"},
                     {"@context", td::td_context_v1_1}})
                  .dump());
    EXPECT_NO_THROW(td.validate());
}

TEST(Thing, PopulatedBuilderRoundTripsThroughJson) {
    td::ThingBuilder builder("Sensor");
    builder.id("urn:example:sensor")
        .description("A lab sensor")
        .base("https://example.test/")
        .version("2.0")
        .semantic_type("ex:Sensor")
        .semantic_type("ex:Device")
        .context_entry(td::ContextEntry::of_uri("https://example.test/context"))
        .context_prefix("ex", "https://example.test/vocab#")
        .property("temperature",
                  td::PropertyBuilder(DataSchema::of(DataType::number))
                      .form(Form("/temperature").with_op(td::OneOrMany<Operation>(Operation::readproperty))))
        .action("reset", td::ActionBuilder().form(
                             Form("/reset").with_op(td::OneOrMany<Operation>(Operation::invokeaction))))
        .event("alarm", td::EventBuilder().form(
                            Form("/alarm").with_op(td::OneOrMany<Operation>(Operation::subscribeevent))))
        .link(td::Link("/manual").with_rel("help"))
        .form(Form("/properties").with_op(td::OneOrMany<Operation>(Operation::readallproperties)))
        .term("ex:vendor", "Example Labs");
    const auto td = builder.build();
    const auto document = td::to_json(td);
    EXPECT_EQ(document["id"], "urn:example:sensor");
    EXPECT_EQ(document["version"]["instance"], "2.0");
    EXPECT_EQ(document["@type"], Value({"ex:Sensor", "ex:Device"}));
    EXPECT_EQ(document["@context"], Value({td::td_context_v1_1,
                                           "https://example.test/context",
                                           {{"ex", "https://example.test/vocab#"}}}));
    EXPECT_EQ(document["properties"]["temperature"]["type"], "number");
    EXPECT_EQ(document["actions"]["reset"]["forms"][0]["op"], "invokeaction");
    EXPECT_EQ(document["events"]["alarm"]["forms"][0]["op"], "subscribeevent");
    EXPECT_EQ(document["links"][0]["rel"], "help");
    EXPECT_EQ(document["forms"][0]["op"], "readallproperties");
    EXPECT_EQ(document["ex:vendor"], "Example Labs");
    const auto decoded = td::thing_description_from_json(document);
    EXPECT_EQ(decoded, td);
    EXPECT_NO_THROW(decoded.validate());
}

TEST(Thing, SecurityDefinitionsAndReferencesAreChecked) {
    using K = td::TdError::Kind;
    EXPECT_EQ(
        td_error([] {
            td::ThingBuilder("Thing").security_definition("basic_sc", td::SecurityScheme::basic()).build();
        }),
        K::missing_security);
    EXPECT_EQ(td_error([] {
                  td::ThingBuilder("Thing")
                      .security_definition("basic_sc", td::SecurityScheme::basic())
                      .security(std::string("unknown"))
                      .build();
              }),
              K::undefined_security);
    EXPECT_EQ(td_error([] {
                  td::ThingBuilder("Thing")
                      .security_definition("combo_sc", td::SecurityScheme::combo_one_of({"missing_sc"}))
                      .security(std::string("combo_sc"))
                      .build();
              }),
              K::undefined_security);
    Form form("/all");
    form.with_op(td::OneOrMany<Operation>(Operation::readallproperties));
    form.security = td::OneOrMany<std::string>(std::string("missing_sc"));
    EXPECT_EQ(td_error([&] { td::ThingBuilder("Thing").form(form).build(); }), K::undefined_security);
    auto td = td::ThingBuilder("Thing").build();
    td.security_definitions.clear();
    EXPECT_EQ(td_error([&] { td.validate(); }), K::no_security_definitions);
    const auto basic = td::ThingBuilder("Thing")
                           .security_definition("basic_sc", td::SecurityScheme::basic())
                           .security(std::string("basic_sc"))
                           .build();
    EXPECT_EQ(td::to_json(basic)["security"], "basic_sc");
}

TEST(Thing, DuplicateNamesAreRejectedWithinEachAffordanceKind) {
    using K = td::TdError::Kind;
    auto property = [] {
        return td::PropertyBuilder(DataSchema::of(DataType::number))
            .form(Form("/p").with_op(td::OneOrMany<Operation>(Operation::readproperty)));
    };
    EXPECT_EQ(td_error([&] {
                  td::ThingBuilder("Thing").property("p", property()).property("p", property()).build();
              }),
              K::duplicate_affordance);
    auto action = [] {
        return td::ActionBuilder().form(
            Form("/a").with_op(td::OneOrMany<Operation>(Operation::invokeaction)));
    };
    EXPECT_EQ(
        td_error([&] { td::ThingBuilder("Thing").action("a", action()).action("a", action()).build(); }),
        K::duplicate_affordance);
    auto event = [] {
        return td::EventBuilder().form(
            Form("/e").with_op(td::OneOrMany<Operation>(Operation::subscribeevent)));
    };
    EXPECT_EQ(td_error([&] { td::ThingBuilder("Thing").event("e", event()).event("e", event()).build(); }),
              K::duplicate_affordance);
}

TEST(Thing, AffordancesNeedNonEmptyNamesAndForms) {
    using K = td::TdError::Kind;
    EXPECT_EQ(td_error([] {
                  td::ThingBuilder("Thing")
                      .property("", td::PropertyBuilder(DataSchema::of(DataType::number)).form(Form("/p")))
                      .build();
              }),
              K::empty_name);
    EXPECT_EQ(td_error([] {
                  td::ThingBuilder("Thing").property("p", td::PropertyBuilder(DataSchema::any())).build();
              }),
              K::no_forms);
    EXPECT_EQ(td_error([] { td::ThingBuilder("Thing").action("a", td::ActionBuilder()).build(); }),
              K::no_forms);
    EXPECT_EQ(td_error([] { td::ThingBuilder("Thing").event("e", td::EventBuilder()).build(); }),
              K::no_forms);
}

TEST(Thing, OperationScopeIsCheckedForAffordanceAndTopLevelForms) {
    using K = td::TdError::Kind;
    auto with = [](Operation op) { return Form("/x").with_op(td::OneOrMany<Operation>(op)); };
    EXPECT_EQ(td_error([&] {
                  td::ThingBuilder("Thing")
                      .property("p",
                                td::PropertyBuilder(DataSchema::any()).form(with(Operation::invokeaction)))
                      .build();
              }),
              K::misplaced_operation);
    EXPECT_EQ(td_error([&] {
                  td::ThingBuilder("Thing")
                      .action("a", td::ActionBuilder().form(with(Operation::readproperty)))
                      .build();
              }),
              K::misplaced_operation);
    EXPECT_EQ(td_error([&] {
                  td::ThingBuilder("Thing")
                      .event("e", td::EventBuilder().form(with(Operation::invokeaction)))
                      .build();
              }),
              K::misplaced_operation);
    EXPECT_EQ(td_error([&] { td::ThingBuilder("Thing").form(with(Operation::subscribeevent)).build(); }),
              K::misplaced_operation);
}

TEST(Thing, DirectValidationChecksTheContext) {
    auto td = td::ThingBuilder("Thing").build();
    td.context = td::Context::of_uri(td::td_context_v1);
    EXPECT_EQ(td_error([&] { td.validate(); }), td::TdError::Kind::invalid_context);
}

// ---- Docstrings -----------------------------------------------------------------------

TEST(Docstring, SplitsAsTetaWot) {
    EXPECT_EQ(td::split_docstring("One line."),
              (std::pair<std::string, std::string>{"One line.", "One line."}));
    EXPECT_EQ(td::split_docstring("Title.\n\nMore.\nAnd more."),
              (std::pair<std::string, std::string>{"Title.", "More.\nAnd more."}));
    // Without a blank second line the description is the whole text.
    EXPECT_EQ(td::split_docstring("Title.\nMore."),
              (std::pair<std::string, std::string>{"Title.", "Title.\nMore."}));
    EXPECT_EQ(td::split_docstring("  Padded title.  "),
              (std::pair<std::string, std::string>{"Padded title.", "  Padded title.  "}));
}

}  // namespace
