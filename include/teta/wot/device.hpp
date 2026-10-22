// Device actors: synchronous drivers on their own threads, a port of
// teta-wot-core's `device`.
//
// A Device<D> owns a driver D on a dedicated thread, which creates it, opens
// it, runs every call on it one at a time, and closes it. The driver never
// leaves that thread, which suits vendor SDKs and COM objects with thread
// affinity.
//
// Calls are functions sent through a bounded mailbox. Each gets the driver
// and the calling invocation's CancelToken, and runs in the caller's
// invocation scope, so its log events land in the invocation's log. A call
// that throws a std::exception fails with a DeviceError (`driver`); one that
// throws DeviceFault, or anything that isn't a std::exception (C++'s nearest
// thing to a Rust panic), faults the device: later calls fail at once until
// `reset` succeeds.
//
// A driver may have `void open()`, `void close()` and
// `std::function<void()> abort_handle() const`; each is optional.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "teta/wot/cancel.hpp"
#include "teta/wot/context.hpp"

namespace teta::wot {

/// The state of a device.
enum class DeviceState {
    /// Not open (before start, after close, or after a failed open).
    closed,
    /// Being created and opened.
    opening,
    /// Open and taking calls.
    ready,
    /// A call panicked; calls fail until the device is reset.
    faulted,
    /// Being closed.
    closing,
};

/// A device call failed.
class DeviceError : public std::exception {
  public:
    enum class Kind {
        /// The device isn't open.
        not_open,
        /// The device is faulted after a panic.
        faulted,
        /// This call panicked; the device is now faulted.
        panicked,
        /// No answer within the timeout. The call may still run later.
        timeout,
        /// Creating or opening the driver failed.
        open,
        /// The driver failed.
        driver,
        /// A call was made from the device's own thread, which would wait
        /// for itself forever.
        reentrant,
    };

    static DeviceError not_open(std::string name);
    static DeviceError faulted(std::string name, std::string reason);
    static DeviceError panicked(std::string name, std::string message);
    static DeviceError timeout(std::string name, std::chrono::nanoseconds timeout);
    static DeviceError open(std::string name, std::string source);
    static DeviceError driver(std::string message);
    static DeviceError reentrant(std::string name);

    Kind kind() const { return kind_; }
    /// The device's name (empty for `driver`).
    const std::string& name() const { return name_; }
    /// The panic message, fault reason, or the source of an open or driver
    /// failure.
    const std::string& detail() const { return detail_; }
    const char* what() const noexcept override { return message_.c_str(); }

  private:
    DeviceError(Kind kind, std::string name, std::string detail, std::string message)
        : kind_(kind), name_(std::move(name)), detail_(std::move(detail)), message_(std::move(message)) {}
    Kind kind_;
    std::string name_;
    std::string detail_;
    std::string message_;
};

/// Thrown from a device call (or a driver's `open` or `close`), it is a
/// panic: the device becomes faulted.
class DeviceFault : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// Options for a Device.
struct DeviceOptions {
    /// How many calls may wait in the mailbox before callers wait too.
    std::size_t mailbox = 16;
    /// How long a call waits for its answer; forever if not set.
    std::optional<std::chrono::nanoseconds> call_timeout;
    /// Runs first on the device thread, for example to initialise COM.
    std::function<void()> thread_init;
};

/// The name of the device thread this runs on (`device:<name>`), or empty
/// on other threads. (Operating systems may keep only part of a thread's
/// name: Linux keeps 15 bytes.)
std::string device_thread_name();

/// Rust's `Debug` of a Duration: `10s`, `1.5s`, `100ms`, `2µs`, `5ns`.
std::string rust_debug(std::chrono::nanoseconds duration);

namespace detail {

/// A driver, whatever its type, on its device thread.
class DriverBox {
  public:
    virtual ~DriverBox() = default;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual std::function<void()> abort_handle() const = 0;
    virtual void* get() = 0;
};

/// The part of a Device that doesn't depend on its driver's type.
class DeviceCore : public std::enable_shared_from_this<DeviceCore> {
  public:
    using Factory = std::function<std::unique_ptr<DriverBox>()>;

    /// A job for the device thread.
    struct Job {
        /// Runs a call on the driver; returns the panic message if it
        /// panicked, without answering (the device thread answers with
        /// `fail` once the device is faulted).
        std::function<std::optional<std::string>(void* driver)> run;
        /// Answers without running.
        std::function<void(DeviceError)> fail;
        /// Set when the caller stopped waiting.
        std::shared_ptr<std::atomic<bool>> abandoned;
    };

    DeviceCore(Factory factory, DeviceOptions options);

    std::string name() const;
    DeviceState state() const;
    std::size_t queue_depth() const;
    void open(std::string_view name);
    void close();
    void reset();
    bool abort();
    /// Checks that a call can be made now (not faulted, not closed, not from
    /// the device thread); throws DeviceError.
    void check_callable() const;
    /// Queues a call, waiting while the mailbox is full; throws DeviceError
    /// (`not_open`).
    void enqueue(Job job);
    const DeviceOptions& options() const { return options_; }

  private:
    enum class Control { none, reset, close };
    struct Item {
        Control control = Control::none;
        Job job;
        std::shared_ptr<std::promise<void>> reply;
    };

    void push(Item item);
    void run(std::shared_ptr<std::promise<void>> opened);
    void create();

    Factory factory_;
    bool factory_taken_ = false;
    DeviceOptions options_;
    std::unique_ptr<DriverBox> driver_;

    mutable std::mutex mutex_;
    std::condition_variable has_items_;
    std::condition_variable has_room_;
    std::deque<Item> mailbox_;
    bool accepting_ = false;
    DeviceState state_ = DeviceState::closed;
    std::string reason_;
    std::string name_;
    std::thread thread_;
    std::thread::id thread_id_;
    std::function<void()> abort_;
};

template <typename D>
class TypedDriver final : public DriverBox {
  public:
    explicit TypedDriver(std::unique_ptr<D> driver) : driver_(std::move(driver)) {}
    void open() override {
        if constexpr (requires(D& d) { d.open(); }) {
            driver_->open();
        }
    }
    void close() override {
        if constexpr (requires(D& d) { d.close(); }) {
            driver_->close();
        }
    }
    std::function<void()> abort_handle() const override {
        if constexpr (requires(const D& d) { std::function<void()>(d.abort_handle()); }) {
            return std::function<void()>(driver_->abort_handle());
        } else {
            return {};
        }
    }
    void* get() override { return driver_.get(); }

  private:
    std::unique_ptr<D> driver_;
};

}  // namespace detail

/// A synchronous driver running on its own thread, called from other threads
/// through a mailbox.
template <typename D>
class Device {
  public:
    /// A device whose driver `factory` creates: a function returning a `D`
    /// or a `std::unique_ptr<D>`. Nothing runs until the device is opened
    /// (when its Thing starts); the factory then runs on the device thread,
    /// and again on each reset.
    template <typename F>
        requires std::invocable<F&>
    explicit Device(F factory, DeviceOptions options = {})
        : core_(std::make_shared<detail::DeviceCore>(wrap(std::move(factory)), std::move(options))) {}
    /// A device whose driver is default-constructed.
    Device()
        requires std::default_initializable<D>
        : Device([] { return std::make_unique<D>(); }) {}
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    ~Device() {
        try {
            core_->close();
        } catch (...) {
        }
    }

    /// The device's name (`<thing>:<member>` once its Thing has started).
    std::string name() const { return core_->name(); }
    DeviceState state() const { return core_->state(); }
    /// How many calls are waiting in the mailbox.
    std::size_t queue_depth() const { return core_->queue_depth(); }

    /// Starts the device thread, creates the driver and opens it; throws
    /// DeviceError. The runtime opens a Thing's devices before it starts.
    void open(std::string_view name) { core_->open(name); }
    /// Closes the driver and ends the device thread; calls still in the
    /// mailbox run first. Throws DeviceError (`driver`) if closing failed.
    void close() { core_->close(); }
    /// Recreates and reopens the driver on the same thread, for example after
    /// a panic faulted the device. The old driver is closed first.
    void reset() { core_->reset(); }
    /// Calls the driver's abort handle, if it has one; returns whether it
    /// did. Works from any thread while a call is running.
    bool abort() { return core_->abort(); }

    /// Runs `f(D&, const CancelToken&)` (or `f(D&)`) on the driver and
    /// returns its result; throws DeviceError. The token is the calling
    /// invocation's (one that is never cancelled outside an invocation). If
    /// the device has a call timeout and it passes, `timeout` is thrown and
    /// the call is skipped if it hasn't started.
    template <typename F>
    auto call(F f) {
        auto future = post(std::move(f));
        return wait(future);
    }

    /// Queues `f` like `call` and returns its future, without waiting for
    /// the answer (but waiting while the mailbox is full).
    template <typename F>
    auto post(F f) {
        using R = result_of<F>;
        core_->check_callable();
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        auto scope = InvocationScope::current().value_or(InvocationScope::fake());
        const std::string name = core_->name();
        detail::DeviceCore::Job job;
        job.abandoned = std::make_shared<std::atomic<bool>>(false);
        job.fail = [promise](DeviceError error) { promise->set_exception(std::make_exception_ptr(error)); };
        auto function = std::make_shared<F>(std::move(f));
        job.run = [promise, scope, name, function](void* driver) -> std::optional<std::string> {
            F& call = *function;
            std::optional<std::string> panic;
            scope.run([&] {
                try {
                    D& d = *static_cast<D*>(driver);
                    if constexpr (std::is_void_v<R>) {
                        invoke(call, d, scope.cancel_token());
                        promise->set_value();
                    } else {
                        promise->set_value(invoke(call, d, scope.cancel_token()));
                    }
                } catch (const DeviceFault& fault) {
                    panic = fault.what();
                } catch (const std::exception& error) {
                    promise->set_exception(std::make_exception_ptr(DeviceError::driver(error.what())));
                } catch (const char* text) {
                    panic = text;
                } catch (...) {
                    panic = "unknown panic";
                }
            });
            // On a panic, the device thread faults the device, then answers
            // with `fail`, so the caller sees the device faulted.
            return panic;
        };
        Pending<R> pending{std::move(future), job.abandoned};
        core_->enqueue(std::move(job));
        return pending;
    }

    /// A call queued with `post`.
    template <typename R>
    struct Pending {
        std::future<R> future;
        std::shared_ptr<std::atomic<bool>> abandoned;
    };

    /// Waits for a call queued with `post`, with the call timeout.
    template <typename R>
    R wait(Pending<R>& pending) {
        if (const auto& timeout = core_->options().call_timeout) {
            if (pending.future.wait_for(*timeout) == std::future_status::timeout) {
                pending.abandoned->store(true);
                throw DeviceError::timeout(core_->name(), *timeout);
            }
        }
        return pending.future.get();
    }

  private:
    template <typename F>
    static decltype(auto) invoke(F& f, D& driver, const CancelToken& token) {
        if constexpr (std::invocable<F&, D&, const CancelToken&>) {
            return f(driver, token);
        } else {
            return f(driver);
        }
    }

    template <typename F>
    using result_of =
        std::conditional_t<std::invocable<F&, D&, const CancelToken&>,
                           std::invoke_result<F&, D&, const CancelToken&>, std::invoke_result<F&, D&>>::type;

    template <typename F>
    static detail::DeviceCore::Factory wrap(F factory) {
        return [factory = std::move(factory)]() mutable -> std::unique_ptr<detail::DriverBox> {
            using Made = std::invoke_result_t<F&>;
            if constexpr (std::is_same_v<Made, std::unique_ptr<D>>) {
                return std::make_unique<detail::TypedDriver<D>>(factory());
            } else {
                return std::make_unique<detail::TypedDriver<D>>(std::make_unique<D>(factory()));
            }
        };
    }

    std::shared_ptr<detail::DeviceCore> core_;
};

}  // namespace teta::wot
