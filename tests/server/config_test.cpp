// The configuration model, the registry, Things built from their
// configuration, and `ThingServer::from_config`. conformance/scenarios/cli
// compares the command line's answers to whole files with the reference's.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "../temp_folder.hpp"
#include "teta/wot.hpp"

namespace tw = teta::wot;
using tw::json::Value;

namespace config_test {

struct CounterConfig {
    std::int64_t start = 0;
    std::string label = "counter";
    static void describe(tw::StructInfo<CounterConfig>& s) {
        s.deny_unknown_fields();
        s.field("start").defaulted();
        s.field("label").defaulted();
    }
};

class Counter {
  public:
    using Config = CounterConfig;
    explicit Counter(CounterConfig config) : count(config.start), label(config.label) {}
    tw::Prop<std::int64_t> count;
    std::string label;
    static void define(tw::ThingSpec<Counter>& t) { t.property("count", &Counter::count); }
};

class Plain {
  public:
    static void define(tw::ThingSpec<Plain>&) {}
};

class Other {
  public:
    static void define(tw::ThingSpec<Other>&) {}
};

std::string config_error(std::string_view text) {
    try {
        (void)tw::ServerConfig::from_json(text);
    } catch (const tw::ConfigError& error) {
        return error.what();
    }
    ADD_FAILURE() << "accepted " << text;
    return {};
}

std::string build_error(tw::RuntimeBuilder builder) {
    try {
        (void)builder.build();
    } catch (const tw::BuildError& error) {
        EXPECT_EQ(error.kind(), tw::BuildError::Kind::invalid_config);
        return error.what();
    }
    ADD_FAILURE() << "built";
    return {};
}

}  // namespace config_test

using namespace config_test;

TEST(Config, AConfigurationIsReadAsLabThingsReadsIt) {
    const auto config = tw::ServerConfig::from_json(R"({
        "things": {
            "counter": "twins.counter:TestThing",
            "stage": {"class": "sangaboard:Stage", "kwargs": {"port": "COM3"}},
            "autofocus": {"cls": "autofocus:Autofocus", "args": [1, "two"],
                          "thing_slots": {"camera": "cam2", "lamp": null, "stages": ["a", "b"]}}
        },
        "settings_folder": "./data",
        "api_prefix": "/api/v1",
        "enable_global_lock": "yes",
        "global_lock_log_level": "WARNING",
        "application_config": {"lab": "B12"},
        "server_id": "lab-1",
        "wire_profile": "wot",
        "mdns": 1,
        "extra": true
    })");
    ASSERT_EQ(config.things.size(), 3u);
    EXPECT_EQ(config.things[0].first, "counter");
    EXPECT_EQ(config.things[0].second.cls, "twins.counter:TestThing");
    EXPECT_EQ(config.things[1].second.cls, "sangaboard:Stage");
    EXPECT_EQ(tw::json::dump(config.things[1].second.kwargs), R"({"port":"COM3"})");
    const auto& autofocus = config.things[2].second;
    EXPECT_EQ(autofocus.cls, "autofocus:Autofocus");
    EXPECT_EQ(autofocus.args.size(), 2u);
    ASSERT_EQ(autofocus.thing_slots.size(), 3u);
    EXPECT_EQ(autofocus.thing_slots[0].second, tw::SlotSelection::name("cam2"));
    EXPECT_EQ(autofocus.thing_slots[1].second, tw::SlotSelection::nothing());
    EXPECT_EQ(autofocus.thing_slots[2].second, tw::SlotSelection::names({"a", "b"}));
    EXPECT_EQ(config.settings_folder_or_default(), "./data");
    EXPECT_EQ(config.api_prefix, "/api/v1");
    EXPECT_TRUE(config.enable_global_lock);
    EXPECT_EQ(config.global_lock_log_level, tw::log::Level::warn);
    EXPECT_EQ(tw::json::dump(*config.application_config), R"({"lab":"B12"})");
    EXPECT_EQ(config.server_id, "lab-1");
    EXPECT_EQ(config.wire_profile, tw::http::WireProfile::wot);
    EXPECT_TRUE(config.mdns);
    EXPECT_EQ(config.ignored_keys, std::vector<std::string>{"extra"});
}

TEST(Config, TheDefaults) {
    const auto config = tw::ServerConfig::from_json(R"({"things": {}})");
    EXPECT_EQ(config.settings_folder_or_default(), "./settings");
    EXPECT_EQ(config.api_prefix, "");
    EXPECT_FALSE(config.enable_global_lock);
    EXPECT_EQ(config.global_lock_log_level, tw::log::Level::info);
    EXPECT_FALSE(config.application_config);
    EXPECT_FALSE(config.server_id);
    EXPECT_FALSE(config.wire_profile);
    EXPECT_FALSE(config.security);
    EXPECT_FALSE(config.mdns);
}

TEST(Config, BooleansAreLaxAsPydanticsAre) {
    for (const auto* yes : {"true", "1", "\"on\"", "\"T\"", "\"Yes\"", "\"y\""}) {
        EXPECT_TRUE(tw::ServerConfig::from_json(std::string(R"({"things": {}, "mdns": )") + yes + "}").mdns)
            << yes;
    }
    for (const auto* no : {"false", "0", "\"OFF\"", "\"f\"", "\"no\"", "\"0\""}) {
        EXPECT_FALSE(tw::ServerConfig::from_json(std::string(R"({"things": {}, "mdns": )") + no + "}").mdns)
            << no;
    }
    for (const auto* neither : {"2", "1.0", "\"maybe\"", "null", "[]"}) {
        EXPECT_EQ(config_error(std::string(R"({"things": {}, "mdns": )") + neither + "}"),
                  "1 validation error for ThingServerConfig\nmdns\n  Input should be a valid boolean")
            << neither;
    }
}

TEST(Config, EveryProblemIsReportedAtOnceAsPydanticReportsIt) {
    EXPECT_EQ(config_error("{"),
              "1 validation error for ThingServerConfig\n  Invalid JSON: EOF while parsing an object at line "
              "1 column 1");
    EXPECT_EQ(config_error("[]"),
              "1 validation error for ThingServerConfig\n  Input should be a valid dictionary or instance of "
              "ThingServerConfig");
    EXPECT_EQ(config_error("{}"), "1 validation error for ThingServerConfig\nthings\n  Field required");
    EXPECT_EQ(
        config_error(R"({"things": {"things": "a:B", "a b": "a:B", "": "a:B", "x": 5, "y": {"kwargs": {}},
                                          "z": {"cls": 1, "args": 2, "kwargs": [], "thing_slots": {"s": 5}}},
                              "settings_folder": 5, "api_prefix": "api", "enable_global_lock": "maybe",
                              "global_lock_log_level": "LOUD", "application_config": [], "server_id": 1,
                              "wire_profile": "x", "security": {"scheme": "basic"}, "mdns": 2})"),
        "19 validation errors for ThingServerConfig\n"
        "things.things.[key]\n  Value error, things is not allowed as the name for a Thing.\n"
        "things.a b.[key]\n  String should match pattern '^([a-zA-Z0-9\\-_]+)$'\n"
        "things..[key]\n  String should have at least 1 character\n"
        "things.x\n  Input should be a Thing class (an import string) or a Thing configuration\n"
        "things.y.cls\n  Field required\n"
        "things.z.cls\n  Input should be a valid string\n"
        "things.z.args\n  Input should be a valid list\n"
        "things.z.kwargs\n  Input should be a valid dictionary\n"
        "things.z.thing_slots.s\n  Input should be a Thing name, a list of names, or null\n"
        "settings_folder\n  Input should be a valid string\n"
        "api_prefix\n  String should match pattern '^(\\/[\\w-]+)*$'\n"
        "enable_global_lock\n  Input should be a valid boolean\n"
        "global_lock_log_level\n  Input should be 'DEBUG', 'INFO', 'WARNING' or 'ERROR'\n"
        "application_config\n  Input should be a valid dictionary\n"
        "server_id\n  Input should be a valid string\n"
        "wire_profile\n  Input should be 'tetathing' or 'wot'\n"
        "security.username\n  Field required\n"
        "security.password_env\n  Field required\n"
        "mdns\n  Input should be a valid boolean");
}

TEST(Config, SecretsComeFromTheEnvironment) {
    const auto config = tw::ServerConfig::from_json(
        R"({"things": {}, "security": {"scheme": "bearer", "token_env": "TETA_WOT_TEST_UNSET_TOKEN"}})");
    ASSERT_TRUE(config.security);
    EXPECT_EQ(config.security->scheme, tw::SecurityConfig::Scheme::bearer);
    try {
        (void)config.security->secret();
        ADD_FAILURE() << "found an unset variable";
    } catch (const tw::ConfigError& error) {
        EXPECT_STREQ(
            error.what(),
            "1 validation error for ThingServerConfig\nsecurity.token_env\n  The environment variable "
            "TETA_WOT_TEST_UNSET_TOKEN isn't set");
    }
    EXPECT_EQ(
        config_error(R"({"things": {}, "security": {"scheme": "digest"}})"),
        "1 validation error for ThingServerConfig\nsecurity.scheme\n  Input should be 'basic' or 'bearer'");
    EXPECT_EQ(
        config_error(R"({"things": {}, "security": {"scheme": "bearer", "token_env": ""}})"),
        "1 validation error for ThingServerConfig\nsecurity.token_env\n  Input should be a non-empty string");
}

TEST(Registry, ClassNamesAreComparedNormalised) {
    EXPECT_EQ(tw::normalise_class_name("a.b:C"), "a.b.C");
    EXPECT_EQ(tw::normalise_class_name("a::b::C"), "a.b.C");
    EXPECT_EQ(tw::normalise_class_name("  a.b.C\t"), "a.b.C");
    tw::ThingRegistry registry;
    registry.add<Plain>({"lab.things:Plain", "lab:Plain"}).add<Counter>("lab.counters:Counter");
    EXPECT_EQ(registry.names(), (std::vector<std::string>{"lab.things.Plain", "lab.Plain", "Plain",
                                                          "lab.counters.Counter", "Counter"}));
    EXPECT_TRUE(registry.contains("lab::things::Plain"));
    EXPECT_TRUE(registry.contains(" lab.counters:Counter "));
    EXPECT_TRUE(registry.contains("Counter"));
    EXPECT_FALSE(registry.contains("lab.Counter"));
}

TEST(Registry, TheLastRegistrationOfANameWins) {
    tw::ThingRegistry registry;
    registry.add<Plain>("lab:Thing").add<Other>("lab:Thing");
    tw::RuntimeBuilder builder;
    ASSERT_TRUE(registry.add_thing(builder, "thing", "lab.Thing", {}, Value::object()));
    EXPECT_FALSE(registry.add_thing(builder, "missing", "lab:Missing", {}, Value::object()));
    auto runtime = builder.build();
    EXPECT_TRUE(runtime.thing("thing")->instance<Other>());
}

TEST(FromConfig, AThingsConfigurationIsReadFromItsKwargsOrArgs) {
    auto runtime = tw::Runtime::builder()
                       .thing_from_config<Counter>("kwargs", Value{{"start", 41}})
                       .thing_from_config<Counter>("defaults", nullptr)
                       .thing_from_args<Counter>("args", {Value(7), Value("seven")}, Value::object())
                       .thing_from_args<Plain>("plain", {}, Value::object())
                       .build();
    EXPECT_EQ(runtime.thing_ref<Counter>("kwargs")->thing().count.get(), 41);
    EXPECT_EQ(runtime.thing_ref<Counter>("defaults")->thing().count.get(), 0);
    EXPECT_EQ(runtime.thing_ref<Counter>("args")->thing().label, "seven");
    EXPECT_TRUE(runtime.thing("plain"));
}

TEST(FromConfig, AConfigurationThatDoesntFitIsRefusedWhenTheRuntimeIsBuilt) {
    EXPECT_EQ(build_error(std::move(tw::Runtime::builder().thing_from_config<Plain>("a", Value{{"x", 1}}))),
              "the configuration of Thing `a` is invalid: unknown field `x`, there are no fields");
    EXPECT_EQ(
        build_error(
            std::move(tw::Runtime::builder().thing_from_args<Plain>("a", {Value(1)}, Value::object()))),
        "the configuration of Thing `a` is invalid: invalid length 1, expected fewer elements in array");
    EXPECT_EQ(build_error(std::move(tw::Runtime::builder().thing_from_config<Counter>("a", Value(5)))),
              "the configuration of Thing `a` is invalid: kwargs must be an object, not 5");
    EXPECT_EQ(build_error(std::move(
                  tw::Runtime::builder().thing_from_args<Counter>("a", {Value(1)}, Value{{"start", 1}}))),
              "the configuration of Thing `a` is invalid: use either args or kwargs, not both: a C++ Thing's "
              "configuration is one typed value");
    EXPECT_EQ(
        build_error(std::move(tw::Runtime::builder().thing_from_args<Counter>(
            "a", {Value(1), Value("x"), Value(3)}, Value::object()))),
        "the configuration of Thing `a` is invalid: invalid length 3, expected fewer elements in array");
    const auto unknown =
        build_error(std::move(tw::Runtime::builder().thing_from_config<Counter>("a", Value{{"x", 1}})));
    EXPECT_TRUE(unknown.starts_with("the configuration of Thing `a` is invalid: unknown field `x`"))
        << unknown;
}

TEST(FromConfig, AServerIsBuiltFromAConfiguration) {
    const testing_support::TemporaryFolder folder;
    tw::ThingRegistry registry;
    registry.add<Counter>("lab:Counter");
    const auto config = tw::ServerConfig::from_value(
        Value{{"things", {{"counter", {{"cls", "lab:Counter"}, {"kwargs", {{"start", 3}}}}}}},
              {"settings_folder", folder.text()},
              {"api_prefix", "/api"},
              {"application_config", {{"lab", "B12"}}}});
    const auto server = tw::ThingServer::from_config(config, registry).build();
    EXPECT_EQ(server.binding()->options().api_prefix, "/api");
    EXPECT_EQ(server.runtime()->thing_ref<Counter>("counter")->thing().count.get(), 3);
    EXPECT_EQ(tw::json::dump(*server.runtime()->server().application_config()), R"({"lab":"B12"})");
    EXPECT_EQ(server.runtime()->server().settings_folder(), folder.path());
}

TEST(FromConfig, AnUnknownClassIsAConfigurationError) {
    tw::ThingRegistry registry;
    registry.add<Counter>("lab:Counter");
    const auto config =
        tw::ServerConfig::from_value(Value{{"things", {{"a", "lab:Counter"}, {"b", "lab:Missing"}}}});
    try {
        (void)tw::ThingServer::from_config(config, registry);
        ADD_FAILURE() << "accepted an unknown class";
    } catch (const tw::ConfigError& error) {
        EXPECT_STREQ(
            error.what(),
            "1 validation error for ThingServerConfig\nthings.b.cls\n  No Thing type is registered as "
            "'lab:Missing'. Registered: lab.Counter, Counter");
    }
}

TEST(FromConfig, SecurityIsRefusedUntilTheBindingSupportsIt) {
#ifdef _WIN32
    _putenv_s("TETA_WOT_TEST_TOKEN", "secret");
#else
    setenv("TETA_WOT_TEST_TOKEN", "secret", 1);
#endif
    const auto config = tw::ServerConfig::from_value(
        Value{{"things", Value::object()},
              {"security", {{"scheme", "bearer"}, {"token_env", "TETA_WOT_TEST_TOKEN"}}}});
    EXPECT_THROW((void)tw::ThingServer::from_config(config, tw::ThingRegistry{}), tw::ConfigError);
}
