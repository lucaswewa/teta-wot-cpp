// teta-wot-core's tests/device.rs, ported: device actors and their lifecycle.

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "teta/wot.hpp"

namespace {

namespace tw = teta::wot;
using namespace std::chrono_literals;

struct Events {
    std::mutex mutex;
    std::vector<std::pair<std::string, std::thread::id>> list;
    void push(std::string event) {
        std::lock_guard lock(mutex);
        list.emplace_back(std::move(event), std::this_thread::get_id());
    }
};

// Not thread-safe, on purpose: every call must stay on the device thread.
struct TestDriver {
    int value = 0;
    std::shared_ptr<Events> events;
    std::shared_ptr<std::atomic<int>> aborts;
    void open() { events->push("open"); }
    void close() { events->push("close"); }
    std::function<void()> abort_handle() const {
        return [aborts = aborts] { ++*aborts; };
    }
};

struct PlainDriver {};

TEST(Device, TheLifecycleAndResetKeepTheDriverOnOneThread) {
    auto events = std::make_shared<Events>();
    auto aborts = std::make_shared<std::atomic<int>>(0);
    tw::DeviceOptions options;
    options.thread_init = [events] { events->push("init"); };
    tw::Device<TestDriver> device(
        [events, aborts] {
            events->push("create");
            return TestDriver{0, events, aborts};
        },
        options);
    EXPECT_EQ(device.name(), "device");
    EXPECT_EQ(device.state(), tw::DeviceState::closed);
    EXPECT_EQ(device.queue_depth(), 0u);
    EXPECT_TRUE(events->list.empty());
    EXPECT_FALSE(device.abort());
    device.close();
    device.open("thing:motor");
    EXPECT_EQ(device.name(), "thing:motor");
    EXPECT_EQ(device.state(), tw::DeviceState::ready);
    const auto thread = device.call([&](TestDriver& driver, const tw::CancelToken& token) {
        EXPECT_FALSE(token.is_cancelled());
        driver.value = 42;
        driver.events->push("call");
        return std::this_thread::get_id();
    });
    EXPECT_NE(thread, std::this_thread::get_id());
    EXPECT_EQ(device.call([](TestDriver& driver) { return driver.value; }), 42);
    EXPECT_TRUE(device.abort());
    EXPECT_EQ(aborts->load(), 1);
    device.reset();
    EXPECT_EQ(device.call([](TestDriver& driver) { return driver.value; }), 0);
    device.close();
    device.close();
    EXPECT_EQ(device.state(), tw::DeviceState::closed);
    EXPECT_FALSE(device.abort());
    std::vector<std::string> names;
    for (const auto& [name, id] : events->list) {
        names.push_back(name);
        EXPECT_EQ(id, thread) << name;
    }
    EXPECT_EQ(names, (std::vector<std::string>{"init", "create", "open", "call", "close", "create", "open",
                                               "close"}));
}

template <typename F>
tw::DeviceError device_error(F&& f) {
    try {
        std::forward<F>(f)();
    } catch (const tw::DeviceError& error) {
        return error;
    }
    ADD_FAILURE() << "no DeviceError";
    return tw::DeviceError::driver("none");
}

TEST(Device, CallsWhenClosedAndASecondOpenAreRefused) {
    tw::Device<PlainDriver> device;
    auto error = device_error([&] { device.call([](PlainDriver&) {}); });
    EXPECT_EQ(error.kind(), tw::DeviceError::Kind::not_open);
    EXPECT_EQ(error.name(), "device");
    EXPECT_EQ(device_error([&] { device.reset(); }).kind(), tw::DeviceError::Kind::not_open);
    device.open("sensor");
    error = device_error([&] { device.open("other"); });
    EXPECT_EQ(error.kind(), tw::DeviceError::Kind::open);
    EXPECT_EQ(error.name(), "other");
    EXPECT_EQ(device.name(), "sensor");
    EXPECT_EQ(device.state(), tw::DeviceState::ready);
    device.close();
    error = device_error([&] { device.call([](PlainDriver&) {}); });
    EXPECT_EQ(error.kind(), tw::DeviceError::Kind::not_open);
    EXPECT_EQ(error.name(), "sensor");
}

TEST(Device, DriverErrorsDoNotFaultTheDevice) {
    tw::Device<PlainDriver> device;
    device.open("sensor");
    EXPECT_EQ(device.call([](PlainDriver&) { return 7; }), 7);
    const auto error =
        device_error([&] { device.call([](PlainDriver&) { throw std::runtime_error("read failed"); }); });
    EXPECT_EQ(error.kind(), tw::DeviceError::Kind::driver);
    EXPECT_STREQ(error.what(), "read failed");
    EXPECT_EQ(device.state(), tw::DeviceState::ready);
    EXPECT_EQ(device.call([](PlainDriver&) { return 9; }), 9);
    device.close();
}

struct CounterDriver {
    int generation = 0;
};

TEST(Device, APanicFaultsCallsUntilAResetRecreatesTheDriver) {
    int generation = 0;
    tw::Device<CounterDriver> device([generation]() mutable { return CounterDriver{++generation}; });
    device.open("motor");
    EXPECT_EQ(device.call([](CounterDriver& d) { return d.generation; }), 1);
    auto error = device_error([&] { device.call([](CounterDriver&) { throw tw::DeviceFault("jammed"); }); });
    EXPECT_EQ(error.kind(), tw::DeviceError::Kind::panicked);
    EXPECT_EQ(error.name(), "motor");
    EXPECT_EQ(error.detail(), "jammed");
    error = device_error([&] { device.call([](CounterDriver&) { throw "must not run"; }); });
    EXPECT_EQ(error.kind(), tw::DeviceError::Kind::faulted);
    EXPECT_EQ(error.detail(), "jammed");
    EXPECT_STREQ(error.what(), "device `motor` is faulted after a panic (jammed); reset it");
    EXPECT_EQ(device.state(), tw::DeviceState::faulted);
    device.reset();
    EXPECT_EQ(device.state(), tw::DeviceState::ready);
    EXPECT_EQ(device.call([](CounterDriver& d) { return d.generation; }), 2);
    device.close();
}

enum class Failure { none, error, panic };

void fail(Failure failure) {
    if (failure == Failure::error) {
        throw std::runtime_error("driver failure");
    }
    if (failure == Failure::panic) {
        throw tw::DeviceFault("driver panic");
    }
}

struct FailingDriver {
    Failure on_open = Failure::none;
    Failure on_close = Failure::none;
    void open() { fail(on_open); }
    void close() { fail(on_close); }
};

TEST(Device, FactoryAndOpenFailuresLeaveTheDeviceClosed) {
    for (bool factory_failure : {false, true}) {
        for (Failure failure : {Failure::error, Failure::panic}) {
            tw::Device<FailingDriver> device([=] {
                if (factory_failure) {
                    fail(failure);
                }
                return FailingDriver{failure, Failure::none};
            });
            const auto error = device_error([&] { device.open("broken"); });
            EXPECT_EQ(error.kind(), tw::DeviceError::Kind::open);
            EXPECT_EQ(error.name(), "broken");
            EXPECT_EQ(error.detail(),
                      failure == Failure::error ? "driver failure" : "panicked: driver panic");
            EXPECT_EQ(device.state(), tw::DeviceState::closed);
            EXPECT_EQ(device_error([&] { device.call([](FailingDriver&) {}); }).kind(),
                      tw::DeviceError::Kind::not_open);
            device.close();
        }
    }
}

TEST(Device, CloseFailuresStillCloseTheDevice) {
    for (Failure failure : {Failure::error, Failure::panic}) {
        tw::Device<FailingDriver> device([=] { return FailingDriver{Failure::none, failure}; });
        device.open("broken");
        const auto error = device_error([&] { device.close(); });
        EXPECT_EQ(error.kind(), tw::DeviceError::Kind::driver);
        EXPECT_STREQ(error.what(), failure == Failure::error ? "driver failure" : "panicked: driver panic");
        EXPECT_EQ(device.state(), tw::DeviceState::closed);
        device.close();
    }
}

TEST(Device, AFailedResetCanBeRetriedOnTheSameThread) {
    int attempts = 0;
    tw::Device<FailingDriver> device([attempts]() mutable {
        ++attempts;
        return FailingDriver{attempts == 2 ? Failure::error : Failure::none, Failure::error};
    });
    device.open("motor");
    const auto thread = device.call([](FailingDriver&) { return std::this_thread::get_id(); });
    EXPECT_EQ(device_error([&] { device.reset(); }).kind(), tw::DeviceError::Kind::open);
    EXPECT_EQ(device.state(), tw::DeviceState::closed);
    device.reset();
    EXPECT_EQ(device.call([](FailingDriver&) { return std::this_thread::get_id(); }), thread);
    EXPECT_EQ(device_error([&] { device.close(); }).kind(), tw::DeviceError::Kind::driver);
}

TEST(Device, CallsFromOtherThreadsWorkAndCallsFromItsOwnAreRefused) {
    tw::Device<PlainDriver> device;
    device.open("sensor");
    std::thread([&] { EXPECT_EQ(device.call([](PlainDriver&) { return 5; }), 5); }).join();
    const auto error =
        device.call([&](PlainDriver&) { return device_error([&] { device.call([](PlainDriver&) {}); }); });
    EXPECT_EQ(error.kind(), tw::DeviceError::Kind::reentrant);
    EXPECT_EQ(error.name(), "sensor");
    EXPECT_EQ(device.state(), tw::DeviceState::ready);
    device.close();
}

TEST(Device, CallsGetTheInvocationsCancellationAndLogInItsScope) {
    tw::Device<PlainDriver> device;
    device.open("sensor");
    const auto scope = tw::InvocationScope::fake();
    scope.cancel_token().cancel();
    scope.run([&] {
        device.call([](PlainDriver&, const tw::CancelToken& token) {
            EXPECT_THROW(token.check(), tw::Cancelled);
            tw::log::info("device invocation log");
        });
    });
    EXPECT_FALSE(scope.cancel_token().is_cancelled());
    const auto logs = scope.logs();
    EXPECT_TRUE(
        std::ranges::any_of(logs, [](const auto& log) { return log.message == "device invocation log"; }));
    EXPECT_FALSE(
        device.call([](PlainDriver&, const tw::CancelToken& token) { return token.is_cancelled(); }));
    device.close();
}

TEST(Device, ATimeoutSkipsAQueuedCallButARunningCallFinishes) {
    tw::DeviceOptions options;
    options.mailbox = 0;  // made one
    options.call_timeout = 50ms;
    tw::Device<CounterDriver> device([] { return CounterDriver{0}; }, options);
    device.open("motor");
    std::promise<void> started;
    std::promise<void> release;
    auto released = release.get_future().share();
    auto first = std::async(std::launch::async, [&] {
        return device_error([&] {
            device.call([&](CounterDriver& d) {
                started.set_value();
                released.wait();
                d.generation += 1;
            });
        });
    });
    started.get_future().wait();
    auto second_error = device_error([&] { device.call([](CounterDriver& d) { d.generation += 100; }); });
    const auto first_error = first.get();
    for (const auto& error : {first_error, second_error}) {
        EXPECT_EQ(error.kind(), tw::DeviceError::Kind::timeout);
        EXPECT_EQ(error.name(), "motor");
        EXPECT_STREQ(error.what(), "device `motor` did not answer within 50ms");
    }
    release.set_value();
    EXPECT_EQ(device.call([](CounterDriver& d) { return d.generation; }), 1);
    EXPECT_EQ(device.queue_depth(), 0u);
    EXPECT_EQ(device.state(), tw::DeviceState::ready);
    device.close();
}

TEST(Device, PostReturnsAFuture) {
    tw::Device<CounterDriver> device;
    device.open("counter");
    auto pending = device.post([](CounterDriver& d) { return ++d.generation; });
    EXPECT_EQ(device.wait(pending), 1);
    device.close();
}

TEST(Device, RustDurationsAreWrittenAsRustsDebug) {
    EXPECT_EQ(tw::rust_debug(10s), "10s");
    EXPECT_EQ(tw::rust_debug(1500ms), "1.5s");
    EXPECT_EQ(tw::rust_debug(100ms), "100ms");
    EXPECT_EQ(tw::rust_debug(2us), "2µs");
    EXPECT_EQ(tw::rust_debug(5ns), "5ns");
}

}  // namespace
