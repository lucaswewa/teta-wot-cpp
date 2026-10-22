// The twin Things in C++: ports of conformance/reference-rs's twins (and so
// of the `wot` repository's), served under the same names, so that the C++
// server can be compared with the reference (ADR-0011).
//
// Phase 3 ports seven of the nine; `blob_probe` and `ndarray_probe` follow
// with Blobs (Phase 7) and arrays (Phase 10). Settings are ordinary
// properties until Phase 5.

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "teta/wot.hpp"

namespace twins {

namespace tw = teta::wot;

/// The names the twins are served under, in `twins.TWINS` order, that this
/// phase has.
extern const std::vector<std::string> names;

/// Adds the twin named `name` to a runtime; throws std::invalid_argument for
/// an unknown name.
void add(tw::RuntimeBuilder& builder, std::string_view name);

/// A position in three dimensions.
struct Position {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    static void describe(tw::StructInfo<Position>& s);
    bool operator==(const Position&) const = default;
};

/// A named position, optionally with a fallback.
struct Waypoint {
    std::string name;
    Position position;
    std::optional<Position> fallback;
    static void describe(tw::StructInfo<Waypoint>& s);
};

enum class Mode { fast, slow };

}  // namespace twins

template <>
struct teta::wot::Describe<twins::Mode> {
    static void describe(EnumInfo<twins::Mode>& e) {
        e.value(twins::Mode::fast, "fast").value(twins::Mode::slow, "slow");
    }
};

namespace twins {

/// An offset and a gain.
struct Calibration {
    double offset = 0.0;
    double gain = 1.0;
    static void describe(tw::StructInfo<Calibration>& s);
};

/// mything's `EmptyInput`: the input of an action with only `**kwargs`.
struct EmptyInput {
    std::optional<tw::json::Value> kwargs;
};

}  // namespace twins

template <>
struct teta::wot::Codec<twins::EmptyInput> {
    static constexpr bool referenced = true;
    static json::Value json_schema();
    static json::Value encode(const twins::EmptyInput& input) {
        return input.kwargs ? *input.kwargs : json::Value(nullptr);
    }
    static twins::EmptyInput decode(const json::Value& value);
};

namespace twins {

/// An example Thing with a few affordances.
class MyThing {
  public:
    tw::Prop<std::int64_t> counter{0};
    tw::Prop<std::string> foo{std::string("Example")};

    tw::OrderedMap<std::string> anaction(std::int64_t repeats, std::int64_t undocumented, std::string title,
                                         std::optional<std::vector<std::string>> attempts);
    tw::OrderedMap<std::optional<std::string>> make_a_dict(std::optional<std::string> extra_key,
                                                           std::optional<std::string> extra_value);
    void increment_counter();
    void slowly_increase_counter(tw::ActionCtx& ctx, std::int64_t increments, double delay);
    void action_without_arguments();
    void action_with_only_kwargs(EmptyInput kwargs);

    static void define(tw::ThingSpec<MyThing>& t);
};

/// A Thing that raises exceptions in actions/properties.
class ThingWithBrokenAffordances {
  public:
    std::monostate broken_property() const;
    void broken_action();
    static void define(tw::ThingSpec<ThingWithBrokenAffordances>& t);
};

/// A test thing with a counter property and a couple of actions.
class TestThing {
  public:
    tw::Prop<std::int64_t> counter{0};
    void increment_counter();
    void slowly_increase_counter(tw::ActionCtx& ctx);
    static void define(tw::ThingSpec<TestThing>& t);
};

/// A Thing whose affordances use many different data types.
class TypesThing {
  public:
    tw::Prop<std::int64_t> bounded_int{5};
    tw::Prop<double> open_interval{0.5};
    tw::Prop<std::int64_t> step{10};
    tw::Prop<std::string> word{std::string("abc")};
    tw::Prop<std::optional<std::int64_t>> maybe_int{std::nullopt};
    tw::Prop<Mode> mode{Mode::fast};
    tw::Prop<Position> position{Position{1, 2, 0}};
    tw::Prop<std::vector<std::string>> tags;
    tw::Prop<std::tuple<std::int64_t, std::string>> pair{std::tuple<std::int64_t, std::string>{1, "a"}};
    tw::Prop<std::map<std::string, double>> lookup;

    Position move(Position target, std::optional<double> speed);
    tw::OrderedMap<Position> plan_route(std::vector<Waypoint> waypoints, Mode mode);
    static void define(tw::ThingSpec<TypesThing>& t);
};

/// A Thing for probing the HTTP binding.
class HttpProbe {
  public:
    tw::Prop<std::int64_t> value{0};
    std::int64_t zero();
    std::string slow(tw::ActionCtx& ctx, double seconds);
    void refuse();
    static void define(tw::ThingSpec<HttpProbe>& t);
};

/// A Thing that raises an exception in __enter__.
class ThingThatCantStart {
  public:
    void enter();
    static void define(tw::ThingSpec<ThingThatCantStart>& t);
};

/// A Thing whose settings are saved to disk.
class SettingsProbe {
  public:
    tw::Prop<std::int64_t> count{0};
    tw::Prop<double> ratio{0.5};
    tw::Prop<std::string> label{std::string("probe")};
    tw::Prop<bool> enabled{false};
    tw::Prop<std::vector<std::string>> tags;
    tw::Prop<tw::OrderedMap<double>> limits;
    tw::Prop<Calibration> calibration{Calibration{0.0, 1.0}};
    tw::Prop<std::optional<std::int64_t>> maybe{std::nullopt};

    double exposure() const;
    void set_exposure(double value);
    static void define(tw::ThingSpec<SettingsProbe>& t);

  private:
    mutable std::mutex mutex_;
    double exposure_ = 10.0;
};

/// A Thing with various different properties and actions.
class ThingWithProperties {
  public:
    tw::Prop<std::int64_t> dataprop{0};
    std::int64_t non_property = 0;

    /// An undecorated function that returns an int.
    std::int64_t undecorated() const { return non_property; }
    std::int64_t funcprop() const;
    void set_funcprop(std::int64_t value);
    tw::json::Value increment_dataprop();
    tw::json::Value raise_error();
    tw::json::Value cancel_myself();
    static void define(tw::ThingSpec<ThingWithProperties>& t);
};

}  // namespace twins
