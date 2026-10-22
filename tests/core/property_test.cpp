// teta-wot-core's tests/property.rs, ported: property cells and client-facing
// property operations.

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "teta/wot.hpp"

namespace {

namespace tw = teta::wot;
namespace td = teta::wot::td;
using tw::json::Value;
using namespace std::chrono_literals;

#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

template <typename F>
tw::PropertyError property_error(F&& f) {
    try {
        std::forward<F>(f)();
    } catch (const tw::PropertyError& error) {
        return error;
    }
    ADD_FAILURE() << "no PropertyError";
    return tw::PropertyError::failed("none");
}

TEST(Prop, ReadsUpdatesAndResetsToItsOriginalDefault) {
    tw::Prop<std::vector<std::int64_t>> prop(std::vector<std::int64_t>{1, 2});
    auto copy = prop.get();
    copy.push_back(99);
    EXPECT_EQ(prop.read([](const auto& v) { return v.size(); }), 2u);
    prop.update([](auto& v) { v.push_back(3); });
    EXPECT_EQ(prop.get(), (std::vector<std::int64_t>{1, 2, 3}));
    EXPECT_EQ(prop.default_value(), (std::vector<std::int64_t>{1, 2}));
    prop.reset();
    EXPECT_EQ(prop.get(), (std::vector<std::int64_t>{1, 2}));
    EXPECT_EQ(tw::Prop<std::int64_t>().get(), 0);
}

TEST(Prop, InvalidWritesAndUpdatesLeaveValueAndWatchersUnchanged) {
    td::Constraints constraints;
    constraints.ge(0).le(10);
    tw::Prop<std::int64_t> prop = tw::Prop<std::int64_t>(5).with_constraints(constraints);
    auto watcher = prop.subscribe();
    EXPECT_EQ(prop.constraints(), constraints);
    const auto schema = prop.data_schema();
    EXPECT_SAME(*schema.minimum, Value(0));
    EXPECT_SAME(*schema.maximum, Value(10));
    for (auto attempt : std::vector<std::function<void()>>{[&] { prop.set(-1); },
                                                           [&] { prop.update([](auto& v) { v = 11; }); }}) {
        const auto error = property_error(attempt);
        ASSERT_EQ(error.kind(), tw::PropertyError::Kind::invalid);
        ASSERT_EQ(error.validation()->issues().size(), 1u);
        EXPECT_TRUE(error.validation()->issues()[0].loc.empty());
        EXPECT_EQ(prop.get(), 5);
        EXPECT_FALSE(watcher.has_changed());
    }
    prop.set(10);
    EXPECT_TRUE(watcher.has_changed());
    EXPECT_EQ(watcher.get(), 10);
    prop.reset();
    EXPECT_EQ(watcher.get(), 5);
}

TEST(Prop, WatchersSeeTheLatestValueAndSameValueWritesNotify) {
    tw::Prop<std::string> prop(std::string("initial"));
    auto first = prop.subscribe();
    auto second = prop.subscribe();
    prop.set("intermediate");
    prop.set("latest");
    EXPECT_EQ(first.get(), "latest");
    EXPECT_EQ(second.get(), "latest");
    EXPECT_FALSE(first.has_changed());
    prop.set("latest");
    EXPECT_TRUE(first.has_changed());
    // And a watcher can wait for a change from another thread.
    (void)second.get();
    EXPECT_FALSE(second.wait_changed(5ms));
    std::thread writer([&] {
        std::this_thread::sleep_for(10ms);
        prop.set("later");
    });
    EXPECT_TRUE(second.wait_changed(2s));
    EXPECT_EQ(second.get(), "later");
    writer.join();
}

TEST(Prop, StringConstraintsRejectBadValuesWithoutMutation) {
    td::Constraints constraints;
    constraints.min_length(2).max_length(4).pattern("^[A-Z]+$");
    tw::Prop<std::string> prop = tw::Prop<std::string>(std::string("AB")).with_constraints(constraints);
    for (const char* value : {"A", "ABCDE", "ab"}) {
        EXPECT_EQ(property_error([&] { prop.set(value); }).kind(), tw::PropertyError::Kind::invalid);
        EXPECT_EQ(prop.get(), "AB");
    }
    prop.set("XYZ");
    EXPECT_EQ(prop.get(), "XYZ");
}

TEST(Prop, InvalidDefinitionAndInvalidDefaultResetAreReported) {
    td::Constraints pattern;
    pattern.pattern("abc");
    tw::Prop<std::int64_t> bad = tw::Prop<std::int64_t>(1).with_constraints(pattern);
    EXPECT_EQ(property_error([&] { (void)bad.data_schema(); }).kind(), tw::PropertyError::Kind::definition);
    EXPECT_EQ(property_error([&] { bad.set(2); }).kind(), tw::PropertyError::Kind::definition);
    EXPECT_EQ(bad.get(), 1);

    td::Constraints ge;
    ge.ge(0);
    tw::Prop<std::int64_t> prop = tw::Prop<std::int64_t>(-1).with_constraints(ge);
    prop.set(2);
    EXPECT_EQ(property_error([&] { prop.reset(); }).kind(), tw::PropertyError::Kind::invalid);
    EXPECT_EQ(prop.get(), 2);
}

td::Constraints count_constraints() {
    td::Constraints c;
    c.ge(0).le(10);
    return c;
}

class Properties : public tw::Thing<Properties> {
  public:
    tw::Prop<std::int64_t> count = tw::Prop<std::int64_t>(2).with_constraints(count_constraints());
    tw::Prop<std::optional<std::int64_t>> optional{std::optional<std::int64_t>(1)};

    std::int64_t get_count() const { return count.get(); }
    void set_count(std::int64_t value) { count.set(value); }

    static void define(tw::ThingSpec<Properties>& t) {
        t.title("Properties");
        t.property("count", &Properties::count)
            .title("Counter")
            .description("Current count")
            .unit("items")
            .semantic_type("CounterProperty")
            .semantic_type("Quantity");
        t.property("readonly", &Properties::count).read_only();
        t.property("unlocked", &Properties::count).global_lock(false);
        t.property("optional", &Properties::optional);
        t.property("getter", &Properties::get_count).doc("Computed count\n\nRead from the cell.");
        t.property("writable", &Properties::get_count).setter(&Properties::set_count).ge(0).le(10);
        t.property("defaulted", &Properties::get_count).setter(&Properties::set_count).default_value(3);
        t.property("resetter", &Properties::get_count)
            .setter(&Properties::set_count)
            .default_value(3)
            .resetter([](Properties& thing) { thing.count.set(7); });
        t.property("functional_readonly", &Properties::get_count)
            .setter(&Properties::set_count)
            .default_value(3)
            .read_only();
        t.property("resetter_only", &Properties::get_count).resetter([](Properties& thing) {
            thing.count.set(7);
        });
        t.property("blocking", [](Properties& thing) {
            tw::log::info("blocking property log");
            tw::check_cancelled();
            return thing.count.get();
        });
        t.property("getter_error",
                   [](Properties&) -> std::int64_t { throw std::runtime_error("getter failed"); });
        t.property("setter_error", &Properties::get_count)
            .setter([](Properties&, std::int64_t) { throw std::runtime_error("setter failed"); })
            .resetter([](Properties&) { throw std::runtime_error("resetter failed"); });
    }
};

struct Fixture {
    std::shared_ptr<Properties> thing = std::make_shared<Properties>();
    tw::Runtime runtime;

    explicit Fixture(bool global_lock)
        : runtime(tw::Runtime::builder()
                      .global_lock(global_lock)
                      .global_lock_timeout(10ms)
                      .thing("counter", thing)
                      .build()) {}

    const tw::PropertyEntry& entry(std::string_view name) const {
        return *runtime.thing("counter")->property(name);
    }
};

void expect_invalid(const std::function<void()>& attempt, const Value& value, std::string_view kind) {
    const auto error = property_error(attempt);
    ASSERT_EQ(error.kind(), tw::PropertyError::Kind::invalid) << error.what();
    ASSERT_EQ(error.validation()->issues().size(), 1u);
    const auto& issue = error.validation()->issues()[0];
    EXPECT_EQ(issue.loc, tw::Loc{"body"});
    EXPECT_SAME(issue.input, value);
    EXPECT_EQ(issue.type, kind);
}

TEST(Properties, DataPropertyMetadataAndClientWritesMatchTheCell) {
    Fixture f(false);
    const auto& property = f.entry("count");
    EXPECT_EQ(property.name(), "count");
    EXPECT_EQ(property.title(), "Counter");
    EXPECT_EQ(property.description(), "Current count");
    EXPECT_EQ(property.unit(), "items");
    EXPECT_EQ(property.semantic_types(), (std::vector<std::string>{"CounterProperty", "Quantity"}));
    EXPECT_FALSE(property.is_read_only());
    EXPECT_TRUE(property.is_observable());
    EXPECT_TRUE(property.is_resettable());
    EXPECT_SAME(*property.default_value(), Value(2));
    EXPECT_SAME(*property.data_schema().maximum, Value(10));
    EXPECT_SAME(property.read(), Value(2));
    property.write("6");
    EXPECT_EQ(f.thing->count.get(), 6);
    EXPECT_SAME(property.read(), Value(6));
    property.reset();
    EXPECT_EQ(f.thing->count.get(), 2);
}

TEST(Properties, ClientValidationRejectsNullAndBadValuesBeforeMutating) {
    Fixture f(false);
    for (const char* name : {"count", "writable"}) {
        const auto& property = f.entry(name);
        expect_invalid([&] { property.write(nullptr); }, nullptr, "missing");
        expect_invalid([&] { property.write(11); }, 11, "less_than_equal");
        expect_invalid([&] { property.write("nope"); }, "nope", "int_parsing");
        EXPECT_EQ(f.thing->count.get(), 2);
    }
    // Null is a missing request body, even when the type allows it.
    expect_invalid([&] { f.entry("optional").write(nullptr); }, nullptr, "missing");
    EXPECT_EQ(f.thing->optional.get(), 1);
    f.thing->optional.set(std::nullopt);
    EXPECT_SAME(f.entry("optional").read(), Value(nullptr));
}

TEST(Properties, ReadOnlyPropertiesRefuseClientWritesAndResetsButAllowCppUpdates) {
    Fixture f(false);
    for (const char* name : {"readonly", "getter", "functional_readonly", "resetter_only"}) {
        const auto& property = f.entry(name);
        EXPECT_TRUE(property.is_read_only()) << name;
        EXPECT_FALSE(property.is_resettable()) << name;
        auto error = property_error([&] { property.write(4); });
        EXPECT_EQ(error.kind(), tw::PropertyError::Kind::read_only);
        EXPECT_EQ(std::string(error.what()), "property `" + std::string(name) + "` is read-only");
        EXPECT_EQ(property_error([&] { property.reset(); }).kind(), tw::PropertyError::Kind::read_only);
    }
    EXPECT_EQ(f.thing->count.get(), 2);
    f.thing->count.set(5);
    EXPECT_SAME(f.entry("readonly").read(), Value(5));
}

TEST(Properties, BoundDataPropertiesPublishCppAndClientChanges) {
    Fixture f(false);
    auto subscription = f.runtime.broker()->subscribe("counter", "count");
    auto other = f.runtime.broker()->subscribe("other", "count");
    auto watcher = f.thing->count.subscribe();
    EXPECT_FALSE(subscription.try_recv());
    for (std::int64_t value : {4, 6, 2}) {
        if (value == 4) {
            f.thing->count.set(value);
        } else if (value == 6) {
            f.entry("count").write(value);
        } else {
            f.entry("count").reset();
        }
        const auto message = subscription.try_recv();
        ASSERT_TRUE(message) << "change notification";
        EXPECT_EQ(message->thing, "counter");
        EXPECT_EQ(message->affordance, "count");
        EXPECT_EQ(message->kind, tw::MessageKind::property);
        EXPECT_SAME(message->payload, Value(value));
        EXPECT_EQ(watcher.get(), value);
        EXPECT_FALSE(other.try_recv());
    }
    EXPECT_EQ(property_error([&] { f.thing->count.set(11); }).kind(), tw::PropertyError::Kind::invalid);
    EXPECT_EQ(property_error([&] { f.entry("count").write(-1); }).kind(), tw::PropertyError::Kind::invalid);
    EXPECT_FALSE(subscription.try_recv());
    EXPECT_FALSE(watcher.has_changed());
}

TEST(Properties, FunctionalPropertiesUseSettersDefaultsAndExplicitResetters) {
    Fixture f(false);
    const auto& getter = f.entry("getter");
    EXPECT_EQ(getter.title(), "Computed count");
    EXPECT_EQ(getter.description(), "Read from the cell.");
    EXPECT_EQ(getter.default_value(), std::nullopt);
    EXPECT_FALSE(getter.is_observable());
    const auto& writable = f.entry("writable");
    EXPECT_FALSE(writable.is_read_only());
    EXPECT_FALSE(writable.is_resettable());
    writable.write("8");
    EXPECT_SAME(writable.read(), Value(8));
    const auto error = property_error([&] { writable.reset(); });
    EXPECT_EQ(error.kind(), tw::PropertyError::Kind::not_resettable);
    EXPECT_STREQ(error.what(), "property `writable` can't be reset");
    const auto& defaulted = f.entry("defaulted");
    EXPECT_TRUE(defaulted.is_resettable());
    EXPECT_SAME(*defaulted.default_value(), Value(3));
    defaulted.reset();
    EXPECT_EQ(f.thing->count.get(), 3);
    f.entry("resetter").reset();
    EXPECT_EQ(f.thing->count.get(), 7);  // The resetter wins over the default 3.
}

TEST(Properties, FunctionalGetterSetterAndResetterErrorsReachTheCaller) {
    Fixture f(false);
    const std::vector<std::pair<std::function<void()>, const char*>> cases = {
        {[&] { (void)f.entry("getter_error").read(); }, "getter failed"},
        {[&] { f.entry("setter_error").write(4); }, "setter failed"},
        {[&] { f.entry("setter_error").reset(); }, "resetter failed"},
    };
    for (const auto& [attempt, expected] : cases) {
        const auto error = property_error(attempt);
        EXPECT_EQ(error.kind(), tw::PropertyError::Kind::failed);
        EXPECT_STREQ(error.what(), expected);
    }
    EXPECT_EQ(f.thing->count.get(), 2);
}

TEST(Properties, GettersRunInTheCallersInvocationForCancellationAndLogging) {
    Fixture f(false);
    const auto scope = tw::InvocationScope::fake();
    scope.cancel_token().cancel();
    const auto& property = f.entry("blocking");
    EXPECT_EQ(property_error([&] { scope.run([&] { (void)property.read(); }); }).kind(),
              tw::PropertyError::Kind::cancelled);
    EXPECT_FALSE(scope.cancel_token().is_cancelled());
    const auto logs = scope.logs();
    EXPECT_TRUE(
        std::ranges::any_of(logs, [](const auto& log) { return log.message == "blocking property log"; }));
    EXPECT_SAME(property.read(), Value(2));
}

TEST(Properties, GlobalLockBlocksWritesAndResetsButAllowsReadsAndOptOut) {
    Fixture f(true);
    const auto scope = tw::InvocationScope::fake();
    const auto& lock = f.runtime.global_lock();
    auto guard = lock->try_acquire(scope.lock_owner());
    for (const char* name : {"count", "defaulted"}) {
        const auto& property = f.entry(name);
        EXPECT_EQ(property_error([&] { property.write(4); }).kind(),
                  tw::PropertyError::Kind::global_lock_busy);
        EXPECT_EQ(property_error([&] { property.reset(); }).kind(),
                  tw::PropertyError::Kind::global_lock_busy);
        EXPECT_SAME(property.read(), Value(2));
    }
    EXPECT_EQ(f.thing->count.get(), 2);
    f.entry("unlocked").write(4);
    f.entry("unlocked").reset();
    scope.run([&] {
        f.entry("count").write(5);
        f.entry("count").reset();
    });
    EXPECT_EQ(lock->owner(), scope.lock_owner());
    guard.release();
    f.entry("count").write(6);
    EXPECT_EQ(lock->owner(), std::nullopt);
    expect_invalid([&] { f.entry("count").write(11); }, 11, "less_than_equal");
    EXPECT_EQ(lock->owner(), std::nullopt);
}

td::Constraints bad_pattern() {
    td::Constraints c;
    c.pattern("x");
    return c;
}

struct BadData {
    tw::Prop<std::int64_t> value = tw::Prop<std::int64_t>(0).with_constraints(bad_pattern());
    static void define(tw::ThingSpec<BadData>& t) { t.property("bad", &BadData::value); }
};

struct BadFunctional {
    static void define(tw::ThingSpec<BadFunctional>& t) {
        t.property("bad", [](BadFunctional&) { return std::int64_t{1}; }).pattern("x");
    }
};

TEST(Properties, InvalidConstraintsFailTheBuildWithTheAffordancesName) {
    for (const auto& build : std::vector<std::function<void()>>{
             [] { (void)tw::Runtime::builder().thing<BadData>("broken").build(); },
             [] { (void)tw::Runtime::builder().thing<BadFunctional>("broken").build(); }}) {
        try {
            build();
            FAIL() << "built";
        } catch (const tw::BuildError& error) {
            ASSERT_EQ(error.kind(), tw::BuildError::Kind::definition);
            EXPECT_EQ(error.definition()->thing(), "broken");
            EXPECT_EQ(error.definition()->affordance(), "bad");
            EXPECT_FALSE(error.definition()->message().empty());
        }
    }
}

TEST(Properties, PropertyErrorsHaveTheExpectedProblemDetails) {
    const auto rethrow = [](auto error) { return tw::PropertyError::from(std::make_exception_ptr(error)); };
    const std::vector<std::tuple<tw::PropertyError, const char*, int, bool>> cases = {
        {tw::PropertyError::invalid(tw::ValidationError::missing({"body"}, nullptr)), "ValidationError", 500,
         false},
        {tw::PropertyError::read_only("count"), "ReadOnlyPropertyError", 500, false},
        {tw::PropertyError::not_resettable("count"), "FeatureNotAvailableError", 500, false},
        {rethrow(tw::DeviceError::not_open("motor")), "DeviceError", 500, false},
        {tw::PropertyError::definition("bad"), "PropertyDefinitionError", 500, false},
        {rethrow(std::runtime_error("failed")), "PropertyError", 500, false},
        {rethrow(tw::GlobalLockBusy()), "GlobalLockBusyError", 409, true},
        {rethrow(tw::Cancelled()), "InvocationCancelledError", 500, true},
    };
    for (const auto& [error, title, status, typed] : cases) {
        const auto problem = error.problem();
        EXPECT_EQ(problem.title, title);
        EXPECT_EQ(problem.status, status);
        EXPECT_EQ(problem.detail, std::string(error.what()));
        EXPECT_EQ(problem.type, typed ? std::optional(tw::teta_wot_exception_type(title)) : std::nullopt);
        EXPECT_EQ(problem.instance, std::nullopt);
    }
}

}  // namespace
