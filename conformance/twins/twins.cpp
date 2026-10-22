#include "twins.hpp"

#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace twins {

const std::vector<std::string> names = {"mything",    "broken",         "counter", "types",
                                        "http_probe", "settings_probe", "ws_probe"};

void add(tw::RuntimeBuilder& builder, std::string_view name) {
    const std::string n(name);
    if (name == "mything") {
        builder.thing<MyThing>(n);
    } else if (name == "broken") {
        builder.thing<ThingWithBrokenAffordances>(n);
    } else if (name == "counter") {
        builder.thing<TestThing>(n);
    } else if (name == "types") {
        builder.thing<TypesThing>(n);
    } else if (name == "http_probe") {
        builder.thing<HttpProbe>(n);
    } else if (name == "settings_probe") {
        builder.thing<SettingsProbe>(n);
    } else if (name == "ws_probe") {
        builder.thing<ThingWithProperties>(n);
    } else {
        throw std::invalid_argument("no twin named `" + n + "`");
    }
}

// The reference registry (conformance/reference-rs, `registry`), in its order.
tw::ThingRegistry registry() {
    tw::ThingRegistry registry;
    registry
        .add<MyThing>({"labthings_fastapi.example_things:MyThing", "twins.mything:MyThing", "twins:MyThing"})
        .add<ThingWithBrokenAffordances>({"labthings_fastapi.example_things:ThingWithBrokenAffordances",
                                          "twins.mything:ThingWithBrokenAffordances",
                                          "twins:ThingWithBrokenAffordances"})
        .add<ThingThatCantStart>({"labthings_fastapi.example_things:ThingThatCantStart"})
        .add<TestThing>({"twins.counter:TestThing", "twins:TestThing"})
        .add<TypesThing>({"twins.types_probe:TypesThing", "twins:TypesThing"})
        .add<HttpProbe>({"twins.http_probe:HttpProbe", "twins:HttpProbe"})
        .add<SettingsProbe>({"twins.settings_probe:SettingsProbe", "twins:SettingsProbe"})
        .add<ThingWithProperties>({"twins.ws_probe:ThingWithProperties", "twins:ThingWithProperties"});
    return registry;
}

void Position::describe(tw::StructInfo<Position>& s) {
    s.description("A position in three dimensions.");
    s.field("z").defaulted();
}

void Waypoint::describe(tw::StructInfo<Waypoint>& s) {
    s.description("A named position, optionally with a fallback.");
    s.field("fallback").defaulted();
}

void Calibration::describe(tw::StructInfo<Calibration>& s) {
    s.description("An offset and a gain.");
}

// ---- MyThing (labthings_fastapi.example_things) ------------------------------------

tw::OrderedMap<std::string> MyThing::anaction(std::int64_t, std::int64_t, std::string,
                                              std::optional<std::vector<std::string>>) {
    counter.update([](std::int64_t& c) { ++c; });
    tw::OrderedMap<std::string> out;
    out["end_result"] = "finished!!";
    return out;
}

tw::OrderedMap<std::optional<std::string>> MyThing::make_a_dict(std::optional<std::string> extra_key,
                                                                std::optional<std::string> extra_value) {
    tw::OrderedMap<std::optional<std::string>> out;
    out["key"] = "value";
    if (extra_key) {
        out[*extra_key] = extra_value;
    }
    return out;
}

void MyThing::increment_counter() {
    counter.update([](std::int64_t& c) { ++c; });
}

void MyThing::slowly_increase_counter(tw::ActionCtx& ctx, std::int64_t increments, double delay) {
    for (std::int64_t i = 0; i < increments; ++i) {
        ctx.sleep(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(std::max(delay, 0.0))));
        counter.update([](std::int64_t& c) { ++c; });
    }
}

void MyThing::action_without_arguments() {}

void MyThing::action_with_only_kwargs(EmptyInput) {}

void MyThing::define(tw::ThingSpec<MyThing>& t) {
    t.doc("An example Thing with a few affordances.");
    t.property("counter", &MyThing::counter).read_only().doc("A pointless counter");
    t.property("foo", &MyThing::foo).doc("A pointless string for demo purposes.");
    auto& anaction = t.action<"repeats", "undocumented", "title", "attempts">("anaction", &MyThing::anaction);
    anaction.doc(
        "Quite a complicated action.\n\nThis action has lots of parameters and is designed to confuse my "
        "schema\n"
        "generator. I hope it doesn't!\n\nI might even use some Markdown here:\n\n* If this renders, it "
        "supports "
        "lists\n* With at least two items.\n\nThere is also a parameter and return block to satisfy "
        "docstring "
        "validators.\nThis may be preferable to annotations on the arguments.\n\n:param repeats: How many "
        "times to "
        "do it.\n:param undocumented: There's no description on this field's type hint.\n:param title: A "
        "human-readable title.\n:param attempts: A list of names of attempts.\n\n:return: A dictionary with "
        "strings as keys and values.");
    anaction.param("repeats").description("The number of times to try the action");
    anaction.param("title").default_value(std::string("Untitled")).description("the title of the invocation");
    anaction.param("attempts")
        .defaulted()
        .description("Names for each attempt - I suggest final, Final, FINAL.");
    auto& make_a_dict = t.action<"extra_key", "extra_value">("make_a_dict", &MyThing::make_a_dict);
    make_a_dict.doc(
        "Do something that returns a dict.\n\n:param extra_key: An additional key.\n:param extra_value: An "
        "additional value.\n:return: a dictionary.");
    make_a_dict.param("extra_key").defaulted();
    make_a_dict.param("extra_value").defaulted();
    t.action("increment_counter", &MyThing::increment_counter)
        .doc(
            "Increment the counter property.\n\nThis action doesn't do very much - all it does, in fact,\nis "
            "increment the counter (which may be read using the\n`counter` property).");
    auto& slowly =
        t.action<"increments", "delay">("slowly_increase_counter", &MyThing::slowly_increase_counter);
    slowly.doc(
        "Increment the counter slowly over a minute.\n\n:param increments: how many times to "
        "increment.\n:param "
        "delay: the wait time between increments.");
    slowly.param("increments").default_value(60);
    slowly.param("delay").default_value(1.0);
    t.action("action_without_arguments", &MyThing::action_without_arguments)
        .doc("Do something that takes no arguments.");
    t.action("action_with_only_kwargs", &MyThing::action_with_only_kwargs, tw::whole_input)
        .doc("Do something that takes \\**kwargs.")
        .description(":param \\**kwargs: Keyword arguments.\n        ");
}

// ---- ThingWithBrokenAffordances -------------------------------------------------------

std::monostate ThingWithBrokenAffordances::broken_property() const {
    throw tw::PropertyError::failed("This is a broken property");
}

void ThingWithBrokenAffordances::broken_action() {
    throw std::runtime_error("This is a broken action");
}

void ThingWithBrokenAffordances::define(tw::ThingSpec<ThingWithBrokenAffordances>& t) {
    t.doc("A Thing that raises exceptions in actions/properties.");
    t.property("broken_property", &ThingWithBrokenAffordances::broken_property)
        .doc("Raise an exception when the property is accessed.")
        .description(":raise RuntimeError: every time.\n        ");
    t.action("broken_action", &ThingWithBrokenAffordances::broken_action)
        .doc("Do something that raises an exception.")
        .description(":raise RuntimeError: every time.\n        ");
}

// ---- The quickstart counter -----------------------------------------------------------

void TestThing::increment_counter() {
    counter.update([](std::int64_t& c) { ++c; });
}

void TestThing::slowly_increase_counter(tw::ActionCtx& ctx) {
    for (int i = 0; i < 60; ++i) {
        ctx.sleep(std::chrono::seconds(1));
        counter.update([](std::int64_t& c) { ++c; });
    }
}

void TestThing::define(tw::ThingSpec<TestThing>& t) {
    t.doc("A test thing with a counter property and a couple of actions.");
    t.property("counter", &TestThing::counter).read_only().doc("A pointless counter.");
    t.action("increment_counter", &TestThing::increment_counter)
        .doc(
            "Increment the counter property.\n\nThis action doesn't do very much - all it does, in fact,\nis "
            "increment the counter (which may be read using the\n`counter` property).");
    t.action("slowly_increase_counter", &TestThing::slowly_increase_counter)
        .doc("Increment the counter slowly over a minute.");
}

// ---- TypesThing -----------------------------------------------------------------------

Position TypesThing::move(Position target, std::optional<double>) {
    position.set(target);
    return target;
}

tw::OrderedMap<Position> TypesThing::plan_route(std::vector<Waypoint> waypoints, Mode) {
    tw::OrderedMap<Position> out;
    for (auto& waypoint : waypoints) {
        out[waypoint.name] = waypoint.position;
    }
    return out;
}

void TypesThing::define(tw::ThingSpec<TypesThing>& t) {
    // The TD has the Python docstring as it is, with its indentation.
    t.description(
        "A Thing whose affordances use many different data types.\n\n    It is used to check how data types "
        "are "
        "described in the Thing Description.\n    ");
    t.property("bounded_int", &TypesThing::bounded_int).doc("An integer between 1 and 10.").ge(1).le(10);
    t.property("open_interval", &TypesThing::open_interval)
        .doc("A number strictly between 0 and 1.")
        .gt(0)
        .lt(1);
    t.property("step", &TypesThing::step).doc("A multiple of five.").multiple_of(5);
    t.property("word", &TypesThing::word)
        .doc("A short lower-case word.")
        .min_length(1)
        .max_length(8)
        .pattern("^[a-z]+$");
    t.property("maybe_int", &TypesThing::maybe_int).doc("An integer that may be missing.");
    t.property("mode", &TypesThing::mode).doc("The speed mode.");
    t.property("position", &TypesThing::position).doc("The current position.");
    t.property("tags", &TypesThing::tags).doc("Up to three tags.").max_length(3);
    t.property("pair", &TypesThing::pair).doc("A pair of an integer and a string.");
    t.property("lookup", &TypesThing::lookup).doc("A lookup table from names to numbers.");
    auto& move = t.action<"target", "speed">("move", &TypesThing::move);
    move.doc(
        "Move to a target position.\n\n:param target: Where to move to.\n:param speed: How fast to move, or "
        "the "
        "default speed.\n:return: The final position.");
    move.param("speed").defaulted();
    auto& plan = t.action<"waypoints", "mode">("plan_route", &TypesThing::plan_route);
    plan.doc(
        "Plan a route through some waypoints.\n\n:param waypoints: The waypoints to visit, in order.\n:param "
        "mode: "
        "The speed mode to use.\n:return: The position of each waypoint, by name.");
    plan.param("mode").default_value(Mode::fast);
}

// ---- HttpProbe ------------------------------------------------------------------------

std::int64_t HttpProbe::zero() {
    return 0;
}

std::string HttpProbe::slow(tw::ActionCtx& ctx, double seconds) {
    ctx.sleep(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(std::max(seconds, 0.0))));
    return "done";
}

void HttpProbe::refuse() {
    throw tw::ActionError::handled("Not now.");
}

void HttpProbe::define(tw::ThingSpec<HttpProbe>& t) {
    t.doc("A Thing for probing the HTTP binding.");
    t.property("value", &HttpProbe::value).doc("A writable integer.");
    t.action("zero", &HttpProbe::zero).doc("Return zero.");
    t.action<"seconds">("slow", &HttpProbe::slow)
        .doc("Sleep, unless cancelled.\n\n:param seconds: How long to sleep.\n:return: \"done\".")
        .param("seconds")
        .default_value(10.0);
    t.action("refuse", &HttpProbe::refuse).doc("Fail in an anticipated way.");
}

// ---- ThingThatCantStart ---------------------------------------------------------------

void ThingThatCantStart::enter() {
    throw std::runtime_error("This thing can't start");
}

void ThingThatCantStart::define(tw::ThingSpec<ThingThatCantStart>& t) {
    t.doc("A Thing that raises an exception in __enter__.");
    t.on_start(&ThingThatCantStart::enter);
}

// ---- SettingsProbe ----------------------------------------------------------------------

double SettingsProbe::exposure() const {
    std::lock_guard lock(mutex_);
    return exposure_;
}

void SettingsProbe::set_exposure(double value) {
    std::lock_guard lock(mutex_);
    exposure_ = value;
}

void SettingsProbe::define(tw::ThingSpec<SettingsProbe>& t) {
    t.doc("A Thing whose settings are saved to disk.");
    t.property("count", &SettingsProbe::count).setting().doc("A non-negative integer.").ge(0);
    t.property("ratio", &SettingsProbe::ratio).setting().doc("A number.");
    t.property("label", &SettingsProbe::label).setting().doc("A label.");
    t.property("enabled", &SettingsProbe::enabled).setting().doc("A flag.");
    t.property("tags", &SettingsProbe::tags).setting().doc("Some tags.");
    t.property("limits", &SettingsProbe::limits).setting().doc("Named limits.");
    t.property("calibration", &SettingsProbe::calibration).setting().doc("The calibration.");
    t.property("maybe", &SettingsProbe::maybe).setting().doc("An integer that may be missing.");
    t.property("exposure", &SettingsProbe::exposure)
        .setter(&SettingsProbe::set_exposure)
        .setting()
        .doc("The exposure time, in milliseconds.");
}

// ---- ThingWithProperties ----------------------------------------------------------------

std::int64_t ThingWithProperties::funcprop() const {
    return 0;
}

void ThingWithProperties::set_funcprop(std::int64_t) {}

tw::json::Value ThingWithProperties::increment_dataprop() {
    dataprop.update([](std::int64_t& v) { ++v; });
    return nullptr;
}

tw::json::Value ThingWithProperties::raise_error() {
    dataprop.update([](std::int64_t& v) { ++v; });
    throw std::runtime_error("A deliberate failure.");
}

tw::json::Value ThingWithProperties::cancel_myself() {
    dataprop.update([](std::int64_t& v) { ++v; });
    throw tw::Cancelled();
}

void ThingWithProperties::define(tw::ThingSpec<ThingWithProperties>& t) {
    t.description(
        "A Thing with various different properties and actions.\n\n    This is used by the earlier tests, "
        "ensuring properties may\n    be observed.\n    ");
    t.property("dataprop", &ThingWithProperties::dataprop);
    t.property("funcprop", &ThingWithProperties::funcprop).setter(&ThingWithProperties::set_funcprop);
    t.action("increment_dataprop", &ThingWithProperties::increment_dataprop)
        .doc("Increment the data property.");
    t.action("raise_error", &ThingWithProperties::raise_error)
        .doc("Raise an exception to test for error status.");
    t.action("cancel_myself", &ThingWithProperties::cancel_myself)
        .doc("Increment the data property, then pretend to be cancelled.");
}

}  // namespace twins

namespace teta::wot {

json::Value Codec<twins::EmptyInput>::json_schema() {
    return {{"description",
             "Represent the input of an action that has no required parameters.\n\nThis may be either a "
             "dictionary "
             "or ``None``."},
            {"anyOf",
             json::Value::array({{{"description", "A model representing an object with no required keys."},
                                  {"type", "object"},
                                  {"properties", json::Value::object()},
                                  {"additionalProperties", true}},
                                 {{"type", "null"}}})}};
}

twins::EmptyInput Codec<twins::EmptyInput>::decode(const json::Value& value) {
    if (value.is_null()) {
        return {};
    }
    if (!value.is_object()) {
        codec::invalid_type(value, "struct EmptyObject");
    }
    return {value};
}

}  // namespace teta::wot
