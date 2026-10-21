// The Phase 1 gate: the twins' Thing Descriptions, described in C++ with the
// codecs and assembled by `td::describe`, equal the reference's (teta-wot
// v0.1.0, `conformance/fixtures/http/tds.json` and `tds-wot.json`) byte for
// byte, after the fixtures' normalisation.
//
// The twins are described here as the runtime will describe them from their
// definitions (Phase 3): names, docstrings, types, defaults, constraints.
// `blob_probe` and `ndarray_probe` follow with Blobs (Phase 7) and arrays
// (Phase 10).

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <tuple>
#include <vector>

#include "teta/wot/codec.hpp"
#include "teta/wot/td/constraints.hpp"
#include "teta/wot/td/describe.hpp"

namespace twins {

namespace tw = teta::wot;

/// A position in three dimensions.
struct Position {
    std::int64_t x;
    std::int64_t y;
    std::int64_t z = 0;
    static void describe(tw::StructInfo<Position>& s) {
        s.description("A position in three dimensions.");
        s.field("z").defaulted();
    }
};

/// A named position, optionally with a fallback.
struct Waypoint {
    std::string name;
    Position position;
    std::optional<Position> fallback;
    static void describe(tw::StructInfo<Waypoint>& s) {
        s.description("A named position, optionally with a fallback.");
        s.field("fallback").defaulted();
    }
};

enum class Mode { fast, slow };

/// An offset and a gain.
struct Calibration {
    double offset;
    double gain;
    static void describe(tw::StructInfo<Calibration>& s) { s.description("An offset and a gain."); }
};

/// mything's `EmptyInput`: the input of an action with only `**kwargs`.
struct EmptyInput {};

}  // namespace twins

template <>
struct teta::wot::Describe<twins::Mode> {
    static void describe(EnumInfo<twins::Mode>& e) {
        e.value(twins::Mode::fast, "fast").value(twins::Mode::slow, "slow");
    }
};

template <>
struct teta::wot::Codec<twins::EmptyInput> {
    static constexpr bool referenced = true;
    static json::Value json_schema() {
        return {{"description",
                 "Represent the input of an action that has no required parameters.\n\nThis may be either a "
                 "dictionary "
                 "or ``None``."},
                {"anyOf", json::Value::array(
                              {{{"description", "A model representing an object with no required keys."},
                                {"type", "object"},
                                {"properties", json::Value::object()},
                                {"additionalProperties", true}},
                               {{"type", "null"}}})}};
    }
    static json::Value encode(const twins::EmptyInput&) { return nullptr; }
};

namespace {

using namespace twins;
namespace tw = teta::wot;
namespace td = teta::wot::td;
using tw::json::Value;

// ---- Describing affordances as the runtime will ---------------------------------

// Title and description from a docstring, or the name as title.
void document(std::string& title, std::optional<std::string>& description, const std::string& name,
              const char* doc, const char* description_override) {
    if (doc) {
        auto [t, d] = td::split_docstring(doc);
        title = t;
        description = d;
    } else {
        title = name;
    }
    if (description_override) {
        description = description_override;
    }
}

template <typename T>
td::PropertyInfo data_property(std::string name, const char* doc, const T& default_value,
                               td::Constraints constraints = {}, bool read_only = false) {
    td::PropertyInfo p;
    p.name = std::move(name);
    document(p.title, p.description, p.name, doc, nullptr);
    p.schema = tw::for_type<T>();
    constraints.apply(p.schema);
    p.read_only = read_only;
    p.observable = true;
    p.default_value = tw::encode(default_value);
    return p;
}

template <typename T>
td::PropertyInfo functional_property(std::string name, const char* doc, bool has_setter,
                                     const char* description_override = nullptr) {
    td::PropertyInfo p;
    p.name = std::move(name);
    document(p.title, p.description, p.name, doc, description_override);
    p.schema = tw::for_type<T>();
    p.read_only = !has_setter;
    return p;
}

// An action parameter: its name and schema, and whether it has a default.
struct Param {
    std::string name;
    Value schema;
    bool optional = false;
    std::optional<std::string> description;
    std::optional<Value> default_value;
};

template <typename T>
Param param(std::string name, std::optional<std::string> description = std::nullopt) {
    return {
        std::move(name), tw::Codec<T>::json_schema(), tw::codec::is_optional<T>, std::move(description), {}};
}

template <typename T>
Param param_default(std::string name, const T& value, std::optional<std::string> description = std::nullopt) {
    return {std::move(name), tw::Codec<T>::json_schema(), true, std::move(description), tw::encode(value)};
}

// The input of an action: an object of its parameters that refuses others,
// or NoInput without parameters.
td::DataSchema input_of(const std::vector<Param>& params) {
    if (params.empty()) {
        return tw::for_type<tw::NoInput>();
    }
    std::vector<tw::codec::Field> fields;
    for (const auto& p : params) {
        fields.push_back({p.name, p.schema, !p.optional, p.description, p.default_value});
    }
    return td::from_json_schema(tw::codec::object_schema(fields, std::nullopt, true));
}

template <typename Output>
td::ActionInfo action(std::string name, const char* doc, td::DataSchema input,
                      const char* description_override = nullptr) {
    td::ActionInfo a;
    a.name = std::move(name);
    document(a.title, a.description, a.name, doc, description_override);
    a.input = std::move(input);
    a.output = tw::for_type<Output>();
    return a;
}

// ---- The twins ------------------------------------------------------------------

td::ThingInfo mything() {
    td::ThingInfo t;
    t.title = "MyThing";
    t.description = "An example Thing with a few affordances.";
    t.properties = {
        data_property<std::int64_t>("counter", "A pointless counter", 0, {}, true),
        data_property<std::string>("foo", "A pointless string for demo purposes.", "Example"),
    };
    t.actions = {
        action<tw::OrderedMap<std::string>>(
            "anaction",
            "Quite a complicated action.\n\nThis action has lots of parameters and is designed to confuse my "
            "schema\n"
            "generator. I hope it doesn't!\n\nI might even use some Markdown here:\n\n* If this renders, it "
            "supports "
            "lists\n* With at least two items.\n\nThere is also a parameter and return block to satisfy "
            "docstring "
            "validators.\nThis may be preferable to annotations on the arguments.\n\n:param repeats: How "
            "many times to "
            "do it.\n:param undocumented: There's no description on this field's type hint.\n:param title: A "
            "human-readable title.\n:param attempts: A list of names of attempts.\n\n:return: A dictionary "
            "with strings "
            "as keys and values.",
            input_of({
                param<std::int64_t>("repeats", "The number of times to try the action"),
                param<std::int64_t>("undocumented"),
                param_default<std::string>("title", "Untitled", "the title of the invocation"),
                param_default<std::optional<std::vector<std::string>>>(
                    "attempts", std::nullopt, "Names for each attempt - I suggest final, Final, FINAL."),
            })),
        action<tw::OrderedMap<std::optional<std::string>>>(
            "make_a_dict",
            "Do something that returns a dict.\n\n:param extra_key: An additional key.\n:param extra_value: "
            "An "
            "additional value.\n:return: a dictionary.",
            input_of({
                param_default<std::optional<std::string>>("extra_key", std::nullopt),
                param_default<std::optional<std::string>>("extra_value", std::nullopt),
            })),
        action<std::monostate>(
            "increment_counter",
            "Increment the counter property.\n\nThis action doesn't do very much - all it does, in "
            "fact,\nis increment the counter (which may be read using the\n`counter` property).",
            input_of({})),
        action<std::monostate>(
            "slowly_increase_counter",
            "Increment the counter slowly over a minute.\n\n:param increments: how many times to "
            "increment.\n:param delay: the wait time between increments.",
            input_of({
                param_default<std::int64_t>("increments", 60),
                param_default<double>("delay", 1.0),
            })),
        action<std::monostate>("action_without_arguments", "Do something that takes no arguments.",
                               input_of({})),
        action<std::monostate>("action_with_only_kwargs", "Do something that takes \\**kwargs.",
                               tw::for_type<EmptyInput>(), ":param \\**kwargs: Keyword arguments.\n        "),
    };
    return t;
}

td::ThingInfo broken() {
    td::ThingInfo t;
    t.title = "ThingWithBrokenAffordances";
    t.description = "A Thing that raises exceptions in actions/properties.";
    t.properties = {functional_property<std::monostate>("broken_property",
                                                        "Raise an exception when the property is accessed.",
                                                        false, ":raise RuntimeError: every time.\n        ")};
    t.actions = {action<std::monostate>("broken_action", "Do something that raises an exception.",
                                        input_of({}), ":raise RuntimeError: every time.\n        ")};
    return t;
}

td::ThingInfo counter() {
    td::ThingInfo t;
    t.title = "TestThing";
    t.description = "A test thing with a counter property and a couple of actions.";
    t.properties = {data_property<std::int64_t>("counter", "A pointless counter.", 0, {}, true)};
    t.actions = {
        action<std::monostate>(
            "increment_counter",
            "Increment the counter property.\n\nThis action doesn't do very much - all it does, in "
            "fact,\nis increment the counter (which may be read using the\n`counter` property).",
            input_of({})),
        action<std::monostate>("slowly_increase_counter", "Increment the counter slowly over a minute.",
                               input_of({})),
    };
    return t;
}

td::ThingInfo types() {
    td::ThingInfo t;
    t.title = "TypesThing";
    t.description =
        "A Thing whose affordances use many different data types.\n\n    It is used to "
        "check how data types are described in the Thing Description.\n    ";
    t.properties = {
        data_property<std::int64_t>("bounded_int", "An integer between 1 and 10.", 5,
                                    td::Constraints{}.ge(1).le(10)),
        data_property<double>("open_interval", "A number strictly between 0 and 1.", 0.5,
                              td::Constraints{}.gt(0).lt(1)),
        data_property<std::int64_t>("step", "A multiple of five.", 10, td::Constraints{}.multiple_of(5)),
        data_property<std::string>("word", "A short lower-case word.", "abc",
                                   td::Constraints{}.min_length(1).max_length(8).pattern("^[a-z]+$")),
        data_property<std::optional<std::int64_t>>("maybe_int", "An integer that may be missing.",
                                                   std::nullopt),
        data_property<Mode>("mode", "The speed mode.", Mode::fast),
        data_property<Position>("position", "The current position.", Position{1, 2, 0}),
        data_property<std::vector<std::string>>("tags", "Up to three tags.", {},
                                                td::Constraints{}.max_length(3)),
        data_property<std::tuple<std::int64_t, std::string>>("pair", "A pair of an integer and a string.",
                                                             {1, "a"}),
        data_property<std::map<std::string, double>>("lookup", "A lookup table from names to numbers.", {}),
    };
    t.actions = {
        action<Position>(
            "move",
            "Move to a target position.\n\n:param target: Where to move to.\n:param speed: How fast to "
            "move, or the default speed.\n:return: The final position.",
            input_of(
                {param<Position>("target"), param_default<std::optional<double>>("speed", std::nullopt)})),
        action<tw::OrderedMap<Position>>(
            "plan_route",
            "Plan a route through some waypoints.\n\n:param waypoints: The waypoints to visit, in "
            "order.\n:param mode: "
            "The speed mode to use.\n:return: The position of each waypoint, by name.",
            input_of({param<std::vector<Waypoint>>("waypoints"), param_default<Mode>("mode", Mode::fast)})),
    };
    return t;
}

td::ThingInfo http_probe() {
    td::ThingInfo t;
    t.title = "HttpProbe";
    t.description = "A Thing for probing the HTTP binding.";
    t.properties = {data_property<std::int64_t>("value", "A writable integer.", 0)};
    t.actions = {
        action<std::int64_t>("zero", "Return zero.", input_of({})),
        action<std::string>(
            "slow", "Sleep, unless cancelled.\n\n:param seconds: How long to sleep.\n:return: \"done\".",
            input_of({param_default<double>("seconds", 10.0)})),
        action<std::monostate>("refuse", "Fail in an anticipated way.", input_of({})),
    };
    return t;
}

td::ThingInfo settings_probe() {
    td::ThingInfo t;
    t.title = "SettingsProbe";
    t.description = "A Thing whose settings are saved to disk.";
    t.properties = {
        data_property<std::int64_t>("count", "A non-negative integer.", 0, td::Constraints{}.ge(0)),
        data_property<double>("ratio", "A number.", 0.5),
        data_property<std::string>("label", "A label.", "probe"),
        data_property<bool>("enabled", "A flag.", false),
        data_property<std::vector<std::string>>("tags", "Some tags.", {}),
        data_property<tw::OrderedMap<double>>("limits", "Named limits.", {}),
        data_property<Calibration>("calibration", "The calibration.", Calibration{0.0, 1.0}),
        data_property<std::optional<std::int64_t>>("maybe", "An integer that may be missing.", std::nullopt),
        functional_property<double>("exposure", "The exposure time, in milliseconds.", true),
    };
    return t;
}

td::ThingInfo ws_probe() {
    td::ThingInfo t;
    t.title = "ThingWithProperties";
    t.description =
        "A Thing with various different properties and actions.\n\n    This is used by the "
        "earlier tests, ensuring properties may\n    be observed.\n    ";
    t.properties = {
        data_property<std::int64_t>("dataprop", nullptr, 0),
        functional_property<std::int64_t>("funcprop", nullptr, true),
    };
    t.actions = {
        action<Value>("increment_dataprop", "Increment the data property.", input_of({})),
        action<Value>("raise_error", "Raise an exception to test for error status.", input_of({})),
        action<Value>("cancel_myself", "Increment the data property, then pretend to be cancelled.",
                      input_of({})),
    };
    return t;
}

const std::map<std::string, td::ThingInfo (*)()> described = {
    {"mything", mything},   {"broken", broken},         {"counter", counter},
    {"types", types},       {"http_probe", http_probe}, {"settings_probe", settings_probe},
    {"ws_probe", ws_probe},
};

// ---- Serving them as the reference does ----------------------------------------------

// The TD as teta-wot serves it, for a request with `Host: testserver`.
td::ThingDescription served(const std::string& name, bool wot_profile) {
    td::TdOptions options;
    options.path = "/" + name + "/";
    options.base = "http://testserver/";
    options.id = td::thing_id("conformance", name);
    options.observation = true;
    options.websocket = "ws://testserver/" + name + "/ws";
    options.links = true;
    options.top_level = true;
    options.invocations = "/action_invocations/";
    options.synchronous_actions = wot_profile;
    if (wot_profile) {
        options.profiles = {"https://www.w3.org/2022/wot/profile/http-basic/v1",
                            "https://www.w3.org/2022/wot/profile/http-sse/v1"};
    }
    return td::describe(described.at(name)(), options);
}

// The fixtures' normalisation of UUIDs, numbered by first appearance.
class Normaliser {
  public:
    std::string text(const std::string& raw) {
        static const std::regex uuid("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
        std::string out;
        auto begin = std::sregex_iterator(raw.begin(), raw.end(), uuid);
        std::size_t last = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            out += raw.substr(last, static_cast<std::size_t>(it->position()) - last);
            const std::string found = it->str();
            if (!numbers_.contains(found)) {
                numbers_.emplace(found, numbers_.size());
            }
            out += "<uuid:" + std::to_string(numbers_.at(found)) + ">";
            last = static_cast<std::size_t>(it->position() + it->length());
        }
        return out + raw.substr(last);
    }

  private:
    std::map<std::string, std::size_t> numbers_;
};

void compare_with_fixture(const char* fixture, bool wot_profile) {
    std::ifstream file(std::string(TETA_WOT_FIXTURES) + "/http/" + fixture, std::ios::binary);
    ASSERT_TRUE(file) << fixture;
    const auto data = Value::parse(file);
    Normaliser normaliser;
    int compared = 0;
    for (const auto& step : data["steps"]) {
        const std::string name = step["step"]["path"].get<std::string>().substr(1);
        const std::string thing = name.substr(0, name.size() - 1);
        if (!described.contains(thing)) {
            continue;  // blob_probe, ndarray_probe: later phases (they are the last steps, so the
                       // UUID numbering of the others doesn't depend on them)
        }
        const auto td = served(thing, wot_profile);
        EXPECT_EQ(normaliser.text(tw::json::dump(td::to_json(td))),
                  step["response"]["text"].get<std::string>())
            << thing << " in " << fixture;
        ++compared;
    }
    EXPECT_EQ(compared, 7);
}

TEST(TwinTds, EqualTheReferenceInTheTetathingProfile) {
    compare_with_fixture("tds.json", false);
}

TEST(TwinTds, EqualTheReferenceInTheWotProfile) {
    compare_with_fixture("tds-wot.json", true);
}

TEST(TwinTds, IdsAreTetaWotsDerivation) {
    EXPECT_EQ(td::thing_id("conformance", "counter"), "urn:uuid:f46f31e7-a858-5fd5-91b0-829d229ed6cd");
}

}  // namespace
