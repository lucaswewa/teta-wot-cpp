// teta-wot-core's tests/runtime.rs, ported: runtime registration, lifecycle
// and action execution.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "teta/wot.hpp"

namespace {

namespace tw = teta::wot;
namespace td = teta::wot::td;
using tw::json::Value;
using namespace std::chrono_literals;

#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

/// tokio's Notify: `notify_one` leaves a permit if nobody waits.
class Notify {
  public:
    void notify_one() {
        std::lock_guard lock(mutex_);
        permit_ = true;
        condition_.notify_one();
    }
    void notified() {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [&] { return permit_; });
        permit_ = false;
    }

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool permit_ = false;
};

struct Empty {
    static void define(tw::ThingSpec<Empty>& t) { t.title("Empty"); }
};

TEST(Runtime, AnEmptyRuntimeHasNoThingsOrLockAndItsLifecycleIsHarmless) {
    auto runtime = tw::Runtime::builder().build();
    EXPECT_TRUE(runtime.things().empty());
    EXPECT_EQ(runtime.thing("missing"), nullptr);
    EXPECT_EQ(runtime.global_lock(), nullptr);
    EXPECT_TRUE(runtime.invocations().list().empty());
    runtime.stop();
    runtime.start();
    runtime.shutdown(0s);
}

TEST(Runtime, TheBuilderChecksNamesAndKeepsTheOrder) {
    for (const char* name : {"", "with space", "slash/name", "a.b", "caf\xC3\xA9"}) {
        try {
            (void)tw::Runtime::builder().thing<Empty>(name).build();
            FAIL() << name;
        } catch (const tw::BuildError& error) {
            EXPECT_EQ(error.kind(), tw::BuildError::Kind::invalid_name);
            EXPECT_EQ(error.thing(), name);
        }
    }
    auto runtime = tw::Runtime::builder().thing<Empty>("z_1-A").thing<Empty>("a").build();
    std::vector<std::string> names;
    for (const auto& thing : runtime.things()) {
        names.push_back(thing->name());
    }
    EXPECT_EQ(names, (std::vector<std::string>{"z_1-A", "a"}));
    try {
        (void)tw::Runtime::builder().thing<Empty>("same").thing<Empty>("same").build();
        FAIL();
    } catch (const tw::BuildError& error) {
        EXPECT_EQ(error.kind(), tw::BuildError::Kind::duplicate_thing);
        EXPECT_EQ(error.thing(), "same");
    }
}

template <int Mode>
struct Duplicate {
    static void define(tw::ThingSpec<Duplicate>& t) {
        auto property = [](Duplicate&) { return std::int64_t{1}; };
        auto action = [](Duplicate&) {};
        if constexpr (Mode == 0) {
            t.property("same", property);
            t.property("same", property);
        } else if constexpr (Mode == 1) {
            t.action("same", action);
            t.action("same", action);
        } else {
            t.property("same", property);
            t.action("same", action);
        }
    }
};

TEST(Runtime, DuplicateAffordancesAreRejectedWithinAndAcrossKinds) {
    for (const auto& build : std::vector<std::function<void()>>{
             [] { (void)tw::Runtime::builder().thing<Duplicate<0>>("thing").build(); },
             [] { (void)tw::Runtime::builder().thing<Duplicate<1>>("thing").build(); },
             [] { (void)tw::Runtime::builder().thing<Duplicate<2>>("thing").build(); }}) {
        try {
            build();
            FAIL();
        } catch (const tw::BuildError& error) {
            EXPECT_EQ(error.kind(), tw::BuildError::Kind::duplicate_affordance);
            EXPECT_EQ(error.thing(), "thing");
            EXPECT_EQ(error.name(), "same");
        }
    }
}

// ---- Lifecycle ----------------------------------------------------------------------

struct Events {
    std::mutex mutex;
    std::vector<std::string> list;
    void push(std::string event) {
        std::lock_guard lock(mutex);
        list.push_back(std::move(event));
    }
    std::vector<std::string> get() {
        std::lock_guard lock(mutex);
        return list;
    }
};

struct RecordingDriver {
    std::string label;
    std::shared_ptr<Events> events;
    bool fail_open = false;
    bool fail_close = false;
    void open() {
        events->push("open:" + label);
        if (fail_open) {
            throw std::runtime_error("open failed");
        }
    }
    void close() {
        events->push("close:" + label);
        if (fail_close) {
            throw std::runtime_error("close failed");
        }
    }
};

std::function<RecordingDriver()> driver(const std::string& label, const char* suffix,
                                        std::shared_ptr<Events> events, bool fail_open, bool fail_close) {
    return [=] { return RecordingDriver{label + ":" + suffix, events, fail_open, fail_close}; };
}

class LifecycleThing {
  public:
    LifecycleThing(const std::string& label, std::shared_ptr<Events> events, bool fail_start, bool fail_open,
                   bool fail_close)
        : first(driver(label, "first", events, false, fail_close)),
          second(driver(label, "second", events, fail_open, fail_close)),
          events_(std::move(events)),
          fail_start_(fail_start) {}

    tw::Device<RecordingDriver> first;
    tw::Device<RecordingDriver> second;

    void start(tw::ThingCtx& ctx) {
        EXPECT_EQ(first.state(), tw::DeviceState::ready);
        EXPECT_EQ(second.state(), tw::DeviceState::ready);
        EXPECT_EQ(first.name(), ctx.name() + ":first");
        events_->push("start:" + ctx.name());
        if (fail_start_) {
            throw std::runtime_error("start failed");
        }
    }
    void stop(tw::ThingCtx& ctx) {
        EXPECT_EQ(first.state(), tw::DeviceState::ready);
        EXPECT_EQ(second.state(), tw::DeviceState::ready);
        events_->push("stop:" + ctx.name());
    }

    static void define(tw::ThingSpec<LifecycleThing>& t) {
        t.title("Lifecycle");
        t.device("first", &LifecycleThing::first);
        t.device("second", &LifecycleThing::second);
        t.on_start(&LifecycleThing::start);
        t.on_stop(&LifecycleThing::stop);
    }

  private:
    std::shared_ptr<Events> events_;
    bool fail_start_;
};

TEST(Runtime, StartOpensDevicesFirstAndStopGoesInReverse) {
    auto events = std::make_shared<Events>();
    auto a = std::make_shared<LifecycleThing>("a", events, false, false, false);
    auto b = std::make_shared<LifecycleThing>("b", events, false, false, true);
    auto runtime = tw::Runtime::builder().thing("a", a).thing("b", b).build();
    EXPECT_TRUE(events->get().empty());
    runtime.start();
    runtime.stop();
    runtime.stop();
    EXPECT_EQ(events->get(),
              (std::vector<std::string>{"open:a:first", "open:a:second", "start:a", "open:b:first",
                                        "open:b:second", "start:b", "stop:b", "close:b:second",
                                        "close:b:first", "stop:a", "close:a:second", "close:a:first"}));
    EXPECT_EQ(a->first.state(), tw::DeviceState::closed);
    EXPECT_EQ(b->second.state(), tw::DeviceState::closed);
}

TEST(Runtime, AStartupFailureClosesPartialDevicesAndRollsBackStartedThings) {
    for (bool fail_open : {false, true}) {
        auto events = std::make_shared<Events>();
        auto runtime =
            tw::Runtime::builder()
                .thing("a", std::make_shared<LifecycleThing>("a", events, false, false, false))
                .thing("b", std::make_shared<LifecycleThing>("b", events, !fail_open, fail_open, false))
                .thing("c", std::make_shared<LifecycleThing>("c", events, false, false, false))
                .build();
        try {
            runtime.start();
            FAIL();
        } catch (const tw::StartupError& error) {
            EXPECT_EQ(error.thing(), "b");
            EXPECT_NE(error.error().find(fail_open ? "open failed" : "start failed"), std::string::npos)
                << error.error();
        }
        runtime.stop();
        std::vector<std::string> expected = {"open:a:first", "open:a:second", "start:a", "open:b:first",
                                             "open:b:second"};
        if (!fail_open) {
            expected.insert(expected.end(), {"start:b", "close:b:second"});
        }
        expected.insert(expected.end(), {"close:b:first", "stop:a", "close:a:second", "close:a:first"});
        EXPECT_EQ(events->get(), expected);
        for (const char* name : {"a", "b", "c"}) {
            const auto thing = runtime.thing(name)->instance<LifecycleThing>();
            EXPECT_EQ(thing->first.state(), tw::DeviceState::closed);
            EXPECT_EQ(thing->second.state(), tw::DeviceState::closed);
        }
    }
}

// ---- Actions --------------------------------------------------------------------------

class Actions {
  public:
    tw::Prop<std::int64_t> value{2};
    std::atomic<int> calls{0};
    std::atomic<int> stops{0};
    Notify started;
    Notify release;

    static void define(tw::ThingSpec<Actions>& t) {
        t.title("Action fixture").description("Runtime test fixture.");
        t.property("z_value", &Actions::value)
            .title("Value")
            .description("Stored value")
            .unit("items")
            .semantic_type("Quantity");
        t.property("a_readonly", &Actions::value).read_only();
        t.action(
             "z_echo",
             [](Actions& thing, tw::ActionCtx& ctx, std::int64_t input) {
                 EXPECT_EQ(ctx.thing_name(), "actions");
                 EXPECT_EQ(tw::InvocationScope::current()->id(), ctx.invocation_id());
                 ++thing.calls;
                 tw::log::info("echo action log");
                 return input + 1;
             },
             tw::whole_input)
            .title("Echo")
            .description("Adds one")
            .retention(5s)
            .semantic_type("EchoAction");
        t.action("a_noop", [](Actions&) {});
        t.action(
            "optional", [](Actions&, std::optional<std::int64_t> input) { return input; }, tw::whole_input);
        t.action("unlocked", [](Actions& thing) { ++thing.calls; }).global_lock(false);
        t.action("handled", [](Actions&) { throw tw::ActionError::handled("anticipated failure"); });
        t.action("failed", [](Actions&) { throw std::runtime_error("unexpected failure"); });
        t.action("panicked", [](Actions&) { throw "action panic"; });
        t.action("wait", [](Actions& thing, tw::ActionCtx& ctx) {
            thing.started.notify_one();
            throw ctx.cancelled();
        });
        t.action("stubborn", [](Actions& thing) {
            thing.started.notify_one();
            thing.release.notified();
        });
        t.action(
            "expected",
            [](Actions&, std::int64_t n) -> tw::Expected<std::int64_t> {
                if (n < 0) {
                    return tw::unexpected(tw::ActionError::handled("negative"));
                }
                return n * 2;
            },
            tw::whole_input);
        t.on_stop([](Actions& thing) { ++thing.stops; });
    }
};

struct ActionsFixture {
    std::shared_ptr<Actions> thing = std::make_shared<Actions>();
    tw::Runtime runtime;
    explicit ActionsFixture(bool locked, tw::Clock clock = {})
        : runtime(tw::Runtime::builder()
                      .global_lock(locked)
                      .global_lock_timeout(10ms)
                      .clock(std::move(clock))
                      .thing("actions", thing)
                      .build()) {}
    const tw::ActionEntry& action(std::string_view name) const {
        return *runtime.thing("actions")->action(name);
    }
};

TEST(Runtime, ThingHandlesExposeMetadataOrderedAffordancesAndTheInstance) {
    ActionsFixture f(false);
    const auto* handle = f.runtime.thing("actions");
    EXPECT_EQ(handle->name(), "actions");
    EXPECT_EQ(handle->title(), "Action fixture");
    EXPECT_EQ(handle->description(), "Runtime test fixture.");
    EXPECT_EQ(handle->instance<Actions>(), f.thing);
    EXPECT_EQ(handle->instance<Empty>(), nullptr);
    EXPECT_EQ(handle->property("missing"), nullptr);
    EXPECT_EQ(handle->action("missing"), nullptr);
    std::vector<std::string> properties;
    for (const auto& p : handle->properties()) {
        properties.push_back(p.name());
    }
    EXPECT_EQ(properties, (std::vector<std::string>{"z_value", "a_readonly"}));
    std::vector<std::string> actions;
    for (const auto& a : handle->actions()) {
        actions.push_back(a.name());
    }
    EXPECT_EQ(actions, (std::vector<std::string>{"z_echo", "a_noop", "optional", "unlocked", "handled",
                                                 "failed", "panicked", "wait", "stubborn", "expected"}));
    const auto& action = f.action("z_echo");
    EXPECT_EQ(action.title(), "Echo");
    EXPECT_EQ(action.description(), "Adds one");
    EXPECT_EQ(action.retention(), 5s);
    EXPECT_EQ(tw::td::to_json(action.input_schema())["type"], "integer");
    EXPECT_EQ(tw::td::to_json(action.output_schema())["type"], "integer");
}

TEST(Runtime, TheGlobalLockConfigurationIsApplied) {
    auto runtime = tw::Runtime::builder().global_lock(true).build();
    EXPECT_EQ(runtime.global_lock()->timeout(), 50ms);
    ActionsFixture configured(true);
    EXPECT_EQ(configured.runtime.global_lock()->timeout(), 10ms);
}

TEST(Runtime, AnInvocationRecordsItsValidatedInputContextOutputAndNotifications) {
    ActionsFixture f(false);
    auto subscription = f.runtime.broker()->subscribe("actions", "z_echo");
    const auto invocation = f.action("z_echo").invoke("4");
    EXPECT_SAME(invocation->input(), Value(4));
    EXPECT_EQ(f.runtime.invocations().get(invocation->id()), invocation);
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::completed);
    EXPECT_SAME(*invocation->output(), Value(5));
    EXPECT_EQ(invocation->error(), std::nullopt);
    EXPECT_EQ(f.thing->calls.load(), 1);
    const auto logs = invocation->logs();
    EXPECT_TRUE(std::ranges::any_of(logs, [](const auto& log) { return log.message == "echo action log"; }));
    for (const char* status : {"pending", "running", "completed"}) {
        const auto message = subscription.recv_for(1s);
        ASSERT_TRUE(message);
        EXPECT_EQ(message->thing, "actions");
        EXPECT_EQ(message->affordance, "z_echo");
        EXPECT_EQ(message->kind, tw::MessageKind::action);
        EXPECT_SAME(message->payload, Value(status));
    }
    const auto record = invocation->record();
    ASSERT_TRUE(record.time_started);
    EXPECT_GE(*record.time_completed, *record.time_started);
}

TEST(Runtime, InvalidInputNeitherRegistersNorRunsAnInvocation) {
    ActionsFixture f(false);
    auto subscription = f.runtime.broker()->subscribe("actions", "z_echo");
    for (const auto& [input, kind] :
         std::vector<std::pair<Value, const char*>>{{nullptr, "missing"}, {"bad", "int_parsing"}}) {
        try {
            (void)f.action("z_echo").invoke(input);
            FAIL();
        } catch (const tw::ValidationError& error) {
            ASSERT_EQ(error.issues().size(), 1u);
            EXPECT_EQ(error.issues()[0].type, kind);
            EXPECT_EQ(error.issues()[0].loc, tw::Loc{"body"});
            EXPECT_SAME(error.issues()[0].input, input);
        }
    }
    EXPECT_TRUE(f.runtime.invocations().list().empty());
    EXPECT_FALSE(subscription.try_recv());
    EXPECT_EQ(f.thing->calls.load(), 0);
}

TEST(Runtime, NoInputAndNullableActionsAcceptNullBodies) {
    ActionsFixture f(false);
    for (const auto& [name, input] : std::vector<std::pair<const char*, Value>>{
             {"a_noop", nullptr}, {"a_noop", Value::object()}, {"optional", nullptr}}) {
        const auto invocation = f.action(name).invoke(input);
        EXPECT_EQ(invocation->wait(), tw::InvocationStatus::completed);
        EXPECT_SAME(*invocation->output(), Value(nullptr));
        EXPECT_SAME(invocation->input(), input);
    }
}

TEST(Runtime, ErrorsAndPanicsFinishAsErrorsWithTheRightLogs) {
    ActionsFixture f(false);
    const std::vector<std::tuple<const char*, const char*, const char*, bool>> cases = {
        {"handled", "InvocationError", "anticipated failure", true},
        {"failed", "Error", "unexpected failure", false},
        {"panicked", "Panicked", "The action panicked: action panic", false},
    };
    for (const auto& [name, title, detail, handled] : cases) {
        const auto invocation = f.action(name).invoke(nullptr);
        EXPECT_EQ(invocation->wait(), tw::InvocationStatus::error);
        EXPECT_EQ(invocation->output(), std::nullopt);
        const auto problem = *invocation->error();
        EXPECT_EQ(problem.title, title);
        EXPECT_EQ(problem.detail, detail);
        const auto logs = invocation->logs();
        const auto log = std::ranges::find_if(logs, [&](const auto& l) { return l.message == detail; });
        ASSERT_NE(log, logs.end()) << name;
        EXPECT_EQ(log->levelname, "ERROR");
        EXPECT_EQ(!log->traceback.has_value(), handled);
        EXPECT_EQ(!log->exception_type.has_value(), handled);
    }
}

TEST(Runtime, AnActionMayReturnAnExpected) {
    ActionsFixture f(false);
    const auto good = f.action("expected").invoke(4);
    EXPECT_EQ(good->wait(), tw::InvocationStatus::completed);
    EXPECT_SAME(*good->output(), Value(8));
    const auto bad = f.action("expected").invoke(-1);
    EXPECT_EQ(bad->wait(), tw::InvocationStatus::error);
    EXPECT_EQ(bad->error()->title, "InvocationError");
    EXPECT_EQ(bad->error()->detail, "negative");
}

TEST(Runtime, LockContentionRefusesActionsBeforeRunningAndTheOptOutSucceeds) {
    for (const auto& [level, name] :
         std::vector<std::pair<tw::log::Level, const char*>>{{tw::log::Level::error, "ERROR"},
                                                             {tw::log::Level::warn, "WARNING"},
                                                             {tw::log::Level::info, "INFO"}}) {
        auto thing = std::make_shared<Actions>();
        auto runtime = tw::Runtime::builder()
                           .global_lock(true)
                           .global_lock_timeout(10ms)
                           .global_lock_log_level(level)
                           .thing("actions", thing)
                           .build();
        auto guard = runtime.global_lock()->try_acquire(tw::Uuid::v4());
        const auto* handle = runtime.thing("actions");
        const auto invocation = handle->action("z_echo")->invoke(1);
        EXPECT_EQ(invocation->wait(), tw::InvocationStatus::error);
        EXPECT_EQ(invocation->record().time_started, std::nullopt);
        EXPECT_EQ(invocation->error()->status, 409);
        EXPECT_EQ(thing->calls.load(), 0);
        const auto logs = invocation->logs();
        EXPECT_TRUE(std::ranges::any_of(logs, [&](const auto& log) {
            return log.levelname == name && log.message.find("Global lock was busy") != std::string::npos;
        }));
        const auto unlocked = handle->action("unlocked")->invoke(nullptr);
        EXPECT_EQ(unlocked->wait(), tw::InvocationStatus::completed);
        EXPECT_EQ(thing->calls.load(), 1);
    }
}

TEST(Runtime, AnInvocationInheritsItsParentsLockOwnerAndReleasesItsOwnGuard) {
    ActionsFixture f(true);
    const auto& lock = f.runtime.global_lock();
    const auto scope = tw::InvocationScope::fake();
    auto guard = lock->try_acquire(scope.lock_owner());
    scope.run([&] {
        const auto invocation = f.action("z_echo").invoke(1);
        EXPECT_EQ(invocation->wait(), tw::InvocationStatus::completed);
    });
    EXPECT_EQ(lock->owner(), scope.lock_owner());
    guard.release();
    EXPECT_EQ(lock->owner(), std::nullopt);
}

TEST(Runtime, InvokingExpiresFinishedInvocationsAtTheirRetention) {
    auto now = std::make_shared<std::atomic<std::int64_t>>(0);
    ActionsFixture f(
        false, [now] { return std::chrono::steady_clock::time_point(std::chrono::seconds(now->load())); });
    const auto& action = f.action("z_echo");
    const auto first = action.invoke(1);
    first->wait();
    *now += 4;
    EXPECT_TRUE(f.runtime.invocations().get(first->id()));
    *now += 1;
    const auto second = action.invoke(2);
    EXPECT_FALSE(f.runtime.invocations().get(first->id()));
    EXPECT_TRUE(f.runtime.invocations().get(second->id()));
    second->wait();
}

TEST(Runtime, ShutdownCancelsUnfinishedActionsAndWaitsBeforeStopping) {
    ActionsFixture f(false);
    f.runtime.start();
    const auto invocation = f.action("wait").invoke(nullptr);
    f.thing->started.notified();
    f.runtime.shutdown(2s);
    EXPECT_EQ(invocation->status(), tw::InvocationStatus::cancelled);
    EXPECT_EQ(invocation->error()->title, "InvocationCancelledError");
    EXPECT_EQ(f.thing->stops.load(), 1);
    f.runtime.stop();
    EXPECT_EQ(f.thing->stops.load(), 1);
}

TEST(Runtime, ShutdownStopsAfterTheGraceEvenIfAnActionIgnoresCancellation) {
    ActionsFixture f(false);
    f.runtime.start();
    const auto invocation = f.action("stubborn").invoke(nullptr);
    f.thing->started.notified();
    const auto started = std::chrono::steady_clock::now();
    f.runtime.shutdown(100ms);
    EXPECT_GE(std::chrono::steady_clock::now() - started, 100ms);
    EXPECT_EQ(invocation->status(), tw::InvocationStatus::running);
    EXPECT_TRUE(invocation->cancel_token().is_cancelled());
    EXPECT_EQ(f.thing->stops.load(), 1);
    f.thing->release.notify_one();
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::completed);
}

TEST(Runtime, TheThingDescriptionSortsAffordancesWithMetadataSchemasAndForms) {
    ActionsFixture f(false);
    td::TdOptions options;
    options.path = "/api/actions/";
    options.base = "https://example.com/";
    options.id = "thing_id";
    const auto td = td::to_json(f.runtime.thing("actions")->thing_description(options));
    EXPECT_EQ(td["title"], "Action fixture");
    EXPECT_EQ(td["description"], "Runtime test fixture.");
    EXPECT_EQ(td["base"], "https://example.com/");
    EXPECT_EQ(td["id"], "thing_id");
    std::vector<std::string> properties;
    for (const auto& [key, value] : td["properties"].items()) {
        properties.push_back(key);
    }
    EXPECT_EQ(properties, (std::vector<std::string>{"a_readonly", "z_value"}));
    std::vector<std::string> actions;
    for (const auto& [key, value] : td["actions"].items()) {
        actions.push_back(key);
    }
    EXPECT_TRUE(std::ranges::is_sorted(actions));
    const auto& property = td["properties"]["z_value"];
    EXPECT_EQ(property["title"], "Value");
    EXPECT_EQ(property["description"], "Stored value");
    EXPECT_EQ(property["default"], 2);
    EXPECT_EQ(property["unit"], "items");
    EXPECT_EQ(property["@type"], "Quantity");
    EXPECT_EQ(property["readOnly"], false);
    EXPECT_EQ(property["writeOnly"], false);
    EXPECT_EQ(property["forms"][0]["href"], "/api/actions/z_value");
    EXPECT_SAME(property["forms"][0]["op"], Value::parse(R"(["readproperty","writeproperty"])"));
    EXPECT_EQ(td["properties"]["a_readonly"]["readOnly"], true);
    EXPECT_SAME(td["properties"]["a_readonly"]["forms"][0]["op"], Value::parse(R"(["readproperty"])"));
    const auto& action = td["actions"]["z_echo"];
    EXPECT_EQ(action["title"], "Echo");
    EXPECT_EQ(action["description"], "Adds one");
    EXPECT_EQ(action["@type"], "EchoAction");
    EXPECT_EQ(action["input"]["title"], "z_echo_input");
    EXPECT_EQ(action["output"]["title"], "z_echo_output");
    EXPECT_EQ(action["forms"][0]["href"], "/api/actions/z_echo");
    EXPECT_SAME(action["forms"][0]["op"], Value::parse(R"(["invokeaction"])"));
    EXPECT_EQ(td["securityDefinitions"]["no_security"]["scheme"], "nosec");
    EXPECT_EQ(td["security"], "no_security");
}

td::Constraints bad_pattern() {
    td::Constraints c;
    c.pattern("x");
    return c;
}

struct BadDefinition {
    tw::Prop<std::int64_t> value = tw::Prop<std::int64_t>(0).with_constraints(bad_pattern());
    static void define(tw::ThingSpec<BadDefinition>& t) { t.property("bad", &BadDefinition::value); }
};

TEST(Runtime, AnInvalidAffordanceDefinitionComesFromBuild) {
    try {
        (void)tw::Runtime::builder().thing<BadDefinition>("bad_thing").build();
        FAIL();
    } catch (const tw::BuildError& error) {
        ASSERT_EQ(error.kind(), tw::BuildError::Kind::definition);
        EXPECT_EQ(error.definition()->thing(), "bad_thing");
        EXPECT_EQ(error.definition()->affordance(), "bad");
    }
}

TEST(Runtime, ReservedAndUnusableAffordanceNamesAreRefused) {
    EXPECT_TRUE(tw::affordance_name_problem("ws"));
    EXPECT_TRUE(tw::affordance_name_problem(""));
    EXPECT_TRUE(tw::affordance_name_problem("a b"));
    EXPECT_FALSE(tw::affordance_name_problem("move_to-2"));
}

}  // namespace
