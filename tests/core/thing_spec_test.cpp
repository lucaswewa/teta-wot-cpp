// The authoring API: what teta-wot's tests/macros.rs checks of the macros
// (in-process here; over HTTP in Phase 4), and the rest of what ThingSpec,
// ThingRef, child invocations and services do.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "teta/wot.hpp"

// A named namespace: Boost.PFR refuses types with internal linkage (MSVC).
namespace thing_spec_test {

namespace tw = teta::wot;
namespace td = teta::wot::td;
using tw::json::Value;
using namespace std::chrono_literals;

#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

class Counter : public tw::Thing<Counter> {
  public:
    tw::Prop<std::int64_t> count{2};
    std::atomic<int> starts{0};
    std::atomic<int> stops{0};

    std::int64_t add(std::int64_t step) {
        count.update([&](std::int64_t& c) { c += step; });
        return count.get();
    }
    std::int64_t doubled() const { return count.get() * 2; }
    void set_doubled(std::int64_t value) { count.set(value / 2); }
    void reset_doubled() { count.reset(); }
    void started(tw::ThingCtx& ctx) {
        EXPECT_EQ(ctx.name(), "counter");
        ++starts;
    }
    void stopped() { ++stops; }

    static void define(tw::ThingSpec<Counter>& t) {
        t.property("count", &Counter::count).ge(0).le(10);
        t.action<"step">("add", &Counter::add).param("step").default_value(2);
        t.property("doubled", &Counter::doubled)
            .setter(&Counter::set_doubled)
            .resetter(&Counter::reset_doubled);
        t.on_start(&Counter::started);
        t.on_stop(&Counter::stopped);
    }
};

TEST(ThingSpec, ActionInputsAndTypedCallsWork) {
    auto thing = std::make_shared<Counter>();
    auto runtime = tw::Runtime::builder().thing("counter", thing).build();
    runtime.start();
    EXPECT_EQ(thing->starts.load(), 1);
    const auto* handle = runtime.thing("counter");
    const auto& add = *handle->action("add");
    EXPECT_SAME(td::to_json(add.input_schema()),
                Value::parse(R"({"type":"object","properties":{"step":{"default":2,"type":"integer"}},)"
                             R"("additionalProperties":false})"));
    const auto invocation = add.invoke(Value::object());
    EXPECT_SAME(invocation->input(), Value::parse(R"({"step":2})"));
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::completed);
    EXPECT_SAME(*invocation->output(), Value(4));
    const auto counter = *runtime.thing_ref<Counter>("counter");
    EXPECT_EQ(counter.call(&Counter::add, 3), 7);
    EXPECT_EQ(thing->count.get(), 7);
    EXPECT_THROW(thing->count.set(11), tw::PropertyError);
    // Calls validate like a client's: 7 + 4 = 11 is refused by the property.
    EXPECT_THROW(counter.call(&Counter::add, 4), tw::PropertyError);
    runtime.stop();
    EXPECT_EQ(thing->stops.load(), 1);
}

TEST(ThingSpec, FunctionalPropertiesFromMemberFunctions) {
    auto thing = std::make_shared<Counter>();
    auto runtime = tw::Runtime::builder().thing("counter", thing).build();
    td::TdOptions options;
    options.path = "/counter/";
    const auto td = td::to_json(runtime.thing("counter")->thing_description(options));
    EXPECT_EQ(td["properties"]["count"]["minimum"], 0);
    EXPECT_EQ(td["properties"]["count"]["maximum"], 10);
    const auto& doubled = *runtime.thing("counter")->property("doubled");
    EXPECT_SAME(doubled.read(), Value(4));
    doubled.write(12);
    EXPECT_EQ(thing->count.get(), 6);
    EXPECT_TRUE(doubled.is_resettable());
    doubled.reset();
    EXPECT_EQ(thing->count.get(), 2);
}

struct TestDriver {};

struct DeviceThing {
    tw::Device<TestDriver> driver;
    static void define(tw::ThingSpec<DeviceThing>& t) { t.device("driver", &DeviceThing::driver); }
};

TEST(ThingSpec, DevicesAreOpenedAndClosedWithTheirThing) {
    auto thing = std::make_shared<DeviceThing>();
    auto runtime = tw::Runtime::builder().thing("device", thing).build();
    runtime.start();
    EXPECT_EQ(thing->driver.state(), tw::DeviceState::ready);
    EXPECT_EQ(thing->driver.name(), "device:driver");
    runtime.stop();
    EXPECT_EQ(thing->driver.state(), tw::DeviceState::closed);
}

struct Trip {
    bool was_emitting = false;
};

struct EventThing {
    tw::Event<Trip> tripped;
    tw::Event<std::monostate> ping;
    static void define(tw::ThingSpec<EventThing>& t) {
        t.event("tripped", &EventThing::tripped).title("Interlock tripped");
        t.event("ping", &EventThing::ping).doc("A ping.");
    }
};

TEST(ThingSpec, EventsRegisterTheirSchemaAndDeliverToCppAndTheBroker) {
    auto thing = std::make_shared<EventThing>();
    auto runtime = tw::Runtime::builder().thing("event", thing).build();
    td::TdOptions options;
    options.path = "/event/";
    options.observation = true;
    const auto td = td::to_json(runtime.thing("event")->thing_description(options));
    EXPECT_EQ(td["events"]["tripped"]["title"], "Interlock tripped");
    EXPECT_EQ(td["events"]["tripped"]["data"]["properties"]["was_emitting"]["type"], "boolean");
    EXPECT_FALSE(td["events"]["ping"].contains("data"));
    auto cpp = thing->tripped.subscribe();
    auto broker = runtime.broker()->subscribe("event", "tripped");
    std::thread([&] { thing->tripped.emit(Trip{true}); }).join();
    EXPECT_TRUE(cpp.try_recv()->was_emitting);
    const auto message = broker.try_recv();
    ASSERT_TRUE(message);
    EXPECT_EQ(message->kind, tw::MessageKind::event);
    EXPECT_SAME(message->payload, Value::parse(R"({"was_emitting":true})"));
}

// ---- Parameters, outputs, injection ------------------------------------------------

struct Position {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

struct Calibration {
    double offset = 0.0;
};

class Stage : public tw::Thing<Stage> {
  public:
    Position position;

    Position move_to(tw::ActionCtx& ctx, std::int64_t x, std::int64_t y, std::optional<std::string> label) {
        ctx.check_cancelled();
        position = {x, y};
        if (label) {
            tw::log::info("moved to {}", *label);
        }
        return position;
    }
    void home() { position = {}; }
    double calibrated(const Position& p, tw::Dep<Calibration> calibration) const {
        return static_cast<double>(p.x) + calibration->offset;
    }
    std::vector<std::string> things(tw::Server server) const { return server.thing_names(); }

    static void define(tw::ThingSpec<Stage>& t) {
        t.doc("A stage.");
        t.action<"x", "y", "label">("move_to", &Stage::move_to).doc("Move.\n\nTo a position.");
        t.action("home", &Stage::home);
        t.action("calibrated", &Stage::calibrated, tw::whole_input);
        t.action("things", &Stage::things);
        t.thing_state([](Stage& stage) { return stage.position; });
    }
};

struct StageFixture {
    std::shared_ptr<Stage> stage = std::make_shared<Stage>();
    tw::Runtime runtime = tw::Runtime::builder()
                              .global_lock(true)
                              .thing("stage", stage)
                              .service(std::make_shared<Calibration>(Calibration{0.5}))
                              .application_config(Value::parse(R"({"lab":"B"})"))
                              .build();
    const tw::ActionEntry& action(std::string_view name) const {
        return *runtime.thing("stage")->action(name);
    }
};

TEST(ThingSpec, NamedParametersMakeAnObjectInputThatRefusesOthers) {
    StageFixture f;
    EXPECT_EQ(f.runtime.thing("stage")->description(), "A stage.");
    EXPECT_EQ(f.runtime.thing("stage")->title(), "Stage");
    const auto& move = f.action("move_to");
    EXPECT_EQ(move.title(), "Move.");
    EXPECT_EQ(move.description(), "To a position.");
    EXPECT_SAME(
        td::to_json(move.input_schema()),
        Value::parse(R"({"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"},)"
                     R"("label":{"oneOf":[{"type":"string"},{"type":"null"}]}},"required":["x","y"],)"
                     R"("additionalProperties":false})"));
    EXPECT_EQ(tw::td::to_json(move.output_schema())["properties"]["x"]["type"], "integer");
    const auto invocation = move.invoke(Value::parse(R"({"x":"3","y":4})"));
    EXPECT_SAME(invocation->input(), Value::parse(R"({"x":3,"y":4,"label":null})"));
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::completed);
    EXPECT_SAME(*invocation->output(), Value::parse(R"({"x":3,"y":4})"));
    try {
        (void)move.invoke(Value::parse(R"({"x":1,"z":2})"));
        FAIL();
    } catch (const tw::ValidationError& error) {
        ASSERT_EQ(error.issues().size(), 2u);
        EXPECT_EQ(error.issues()[0].type, "missing");
        EXPECT_EQ(error.issues()[1].type, "extra_forbidden");
    }
    EXPECT_SAME(f.runtime.server().thing_states()["stage"], Value::parse(R"({"x":3,"y":4})"));
}

TEST(ThingSpec, ServerAndServicesAreInjected) {
    StageFixture f;
    EXPECT_SAME(f.action("calibrated").call(Value::parse(R"({"x":2,"y":0})")), Value(2.5));
    EXPECT_SAME(f.action("things").call(nullptr), Value::parse(R"(["stage"])"));
    EXPECT_SAME(*f.runtime.server().application_config(), Value::parse(R"({"lab":"B"})"));
    EXPECT_EQ(f.runtime.server().service<Calibration>()->offset, 0.5);
    EXPECT_EQ(f.runtime.server().service<Position>(), nullptr);
    EXPECT_EQ(f.runtime.server().thing_ref<Stage>("stage")->name(), "stage");
    EXPECT_FALSE(f.runtime.server().thing_ref<Counter>("stage"));
    EXPECT_EQ(f.runtime.server().things_of_type<Stage>().size(), 1u);

    // Without the service, the action fails with a message naming it.
    auto stage = std::make_shared<Stage>();
    auto runtime = tw::Runtime::builder().thing("stage", stage).build();
    const auto invocation =
        runtime.thing("stage")->action("calibrated")->invoke(Value::parse(R"({"x":1,"y":1})"));
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::error);
    EXPECT_EQ(invocation->error()->detail, "the service `Calibration` isn't registered with the server");
}

TEST(ThingSpec, InProcessCallsHoldTheLockForTheCallerAndRunInItsInvocation) {
    StageFixture f;
    const auto stage = *f.runtime.thing_ref<Stage>("stage");
    // The lock is held by someone else: in-process calls are refused.
    {
        auto guard = f.runtime.global_lock()->try_acquire(tw::Uuid::v4());
        EXPECT_THROW(stage.call(&Stage::home), tw::GlobalLockBusy);
    }
    // A caller holding the lock calls through it, and the call logs to the
    // caller's invocation.
    const auto scope = tw::InvocationScope::fake();
    auto guard = f.runtime.global_lock()->try_acquire(scope.lock_owner());
    scope.run([&] {
        const Position p = stage.call(&Stage::move_to, 5, 6, std::optional<std::string>("left"));
        EXPECT_EQ(p.x, 5);
    });
    const auto logs = scope.logs();
    EXPECT_TRUE(std::ranges::any_of(logs, [](const auto& log) { return log.message == "moved to left"; }));
    // Cancelling the caller cancels the call.
    scope.cancel_token().cancel();
    EXPECT_THROW(scope.run([&] { stage.call(&Stage::move_to, 1, 1, std::nullopt); }), tw::Cancelled);
    guard.release();
    // By name, with JSON.
    EXPECT_SAME(stage.call_action("move_to", Value::parse(R"({"x":1,"y":2})")),
                Value::parse(R"({"x":1,"y":2})"));
    EXPECT_THROW((void)stage.call_action("move_to", Value::parse(R"({"x":"one","y":2})")),
                 tw::ValidationError);
    EXPECT_THROW((void)stage.call_action("nothing", nullptr), std::invalid_argument);
    EXPECT_EQ(f.runtime.invocations().list().size(), 0u);
}

// ---- Child invocations and cancellation ----------------------------------------------

class Parent {
  public:
    std::atomic<int> child_cancelled{0};

    std::int64_t run_child(tw::ActionCtx& ctx) {
        auto child = ctx.spawn_child([this](tw::ActionCtx& child_ctx) {
            tw::log::info("child log");
            try {
                child_ctx.sleep(10s);
            } catch (const tw::Cancelled&) {
                ++child_cancelled;
                throw;
            }
            return std::int64_t{1};
        });
        EXPECT_NE(child.id(), ctx.invocation_id());
        return child.join();
    }
    std::int64_t quick_child(tw::ActionCtx& ctx) {
        auto child = ctx.spawn_child([](tw::ActionCtx&) { return std::int64_t{41}; });
        const auto value = child.join() + 1;
        return value;
    }

    static void define(tw::ThingSpec<Parent>& t) {
        t.action("run_child", &Parent::run_child);
        t.action("quick_child", &Parent::quick_child);
    }
};

TEST(ThingSpec, ChildInvocationsReturnTheirResultAndPassOnCancellation) {
    auto parent = std::make_shared<Parent>();
    auto runtime = tw::Runtime::builder().thing("parent", parent).build();
    const auto quick = runtime.thing("parent")->action("quick_child")->invoke(nullptr);
    EXPECT_EQ(quick->wait(), tw::InvocationStatus::completed);
    EXPECT_SAME(*quick->output(), Value(42));

    const auto invocation = runtime.thing("parent")->action("run_child")->invoke(nullptr);
    std::this_thread::sleep_for(50ms);
    runtime.invocations().cancel(invocation->id());
    EXPECT_EQ(invocation->wait(), tw::InvocationStatus::cancelled);
    EXPECT_EQ(parent->child_cancelled.load(), 1);
    // The child's log is its own.
    const auto logs = invocation->logs();
    EXPECT_FALSE(std::ranges::any_of(logs, [](const auto& log) { return log.message == "child log"; }));
}

TEST(ThingSpec, CancellingAFinishedOrUnknownInvocationIsRefused) {
    auto parent = std::make_shared<Parent>();
    auto runtime = tw::Runtime::builder().thing("parent", parent).build();
    const auto quick = runtime.thing("parent")->action("quick_child")->invoke(nullptr);
    quick->wait();
    try {
        runtime.invocations().cancel(quick->id());
        FAIL();
    } catch (const tw::CancelError& error) {
        EXPECT_EQ(error.kind(), tw::CancelError::Kind::not_cancellable);
        EXPECT_STREQ(error.what(), "The invocation is completed and may not be cancelled.");
    }
    const auto unknown = tw::Uuid::v4();
    try {
        runtime.invocations().cancel(unknown);
        FAIL();
    } catch (const tw::CancelError& error) {
        EXPECT_EQ(error.kind(), tw::CancelError::Kind::not_found);
        EXPECT_EQ(std::string(error.what()), "No action invocation found with ID " + unknown.to_string());
    }
}

TEST(ThingSpec, HoldGlobalLockNeedsTheLock) {
    const auto ctx = tw::ActionCtx::detached();
    EXPECT_THROW((void)ctx.hold_global_lock(), tw::GlobalLockNotEnabled);
}

TEST(ThingSpec, ActionErrorsHaveTitlesFromTheirTypes) {
    struct MotorJammed : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    const auto title = [](auto error) {
        return tw::ActionError::from(std::make_exception_ptr(error)).title();
    };
    EXPECT_EQ(title(MotorJammed("jammed")), "MotorJammed");
    EXPECT_EQ(title(std::runtime_error("x")), "Error");
    EXPECT_EQ(title(std::out_of_range("x")), "Error");
    EXPECT_EQ(title(tw::DeviceError::not_open("m")), "DeviceError");
    EXPECT_EQ(title(tw::ValidationError::missing({"body"}, nullptr)), "ValidationError");
    // Causes are joined, as anyhow's `{:#}`, and listed in the traceback.
    try {
        try {
            throw std::runtime_error("serial port closed");
        } catch (...) {
            std::throw_with_nested(MotorJammed("can't move"));
        }
    } catch (...) {
        const auto error = tw::ActionError::from(std::current_exception());
        EXPECT_STREQ(error.what(), "can't move: serial port closed");
        EXPECT_EQ(error.title(), "MotorJammed");  // the wrapped exception's, not the wrapper's
        EXPECT_EQ(error.traceback(), "can't move\n\nCaused by:\n    serial port closed");
    }
}

}  // namespace thing_spec_test
