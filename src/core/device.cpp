#include "teta/wot/device.hpp"

#include <format>

#ifdef __linux__
#include <pthread.h>
#endif

#include "teta/wot/log.hpp"

namespace teta::wot {

namespace {

thread_local std::string this_device_thread;

}  // namespace

std::string device_thread_name() {
    return this_device_thread;
}

std::string rust_debug(std::chrono::nanoseconds duration) {
    const auto ns = static_cast<std::uint64_t>(duration.count());
    auto fractional = [](std::uint64_t whole, std::uint64_t rest, std::uint64_t unit, const char* suffix) {
        std::string out = std::to_string(whole);
        if (rest != 0) {
            std::string digits = std::to_string(rest);
            digits.insert(0, std::to_string(unit).size() - 1 - digits.size(), '0');
            while (!digits.empty() && digits.back() == '0') {
                digits.pop_back();
            }
            out += "." + digits;
        }
        return out + suffix;
    };
    if (ns >= 1'000'000'000) {
        return fractional(ns / 1'000'000'000, ns % 1'000'000'000, 1'000'000'000, "s");
    }
    if (ns >= 1'000'000) {
        return fractional(ns / 1'000'000, ns % 1'000'000, 1'000'000, "ms");
    }
    if (ns >= 1'000) {
        return fractional(ns / 1'000, ns % 1'000, 1'000, "µs");
    }
    return std::to_string(ns) + "ns";
}

DeviceError DeviceError::not_open(std::string name) {
    std::string message = "device `" + name + "` is not open";
    return {Kind::not_open, std::move(name), {}, std::move(message)};
}

DeviceError DeviceError::faulted(std::string name, std::string reason) {
    std::string message = "device `" + name + "` is faulted after a panic (" + reason + "); reset it";
    return {Kind::faulted, std::move(name), std::move(reason), std::move(message)};
}

DeviceError DeviceError::panicked(std::string name, std::string panic) {
    std::string message = "device `" + name + "` panicked: " + panic;
    return {Kind::panicked, std::move(name), std::move(panic), std::move(message)};
}

DeviceError DeviceError::timeout(std::string name, std::chrono::nanoseconds timeout) {
    std::string message = "device `" + name + "` did not answer within " + rust_debug(timeout);
    return {Kind::timeout, std::move(name), {}, std::move(message)};
}

DeviceError DeviceError::open(std::string name, std::string source) {
    std::string message = "device `" + name + "` failed to open: " + source;
    return {Kind::open, std::move(name), std::move(source), std::move(message)};
}

DeviceError DeviceError::driver(std::string message) {
    return {Kind::driver, {}, message, message};
}

DeviceError DeviceError::reentrant(std::string name) {
    std::string message = "device `" + name + "` was called from its own thread";
    return {Kind::reentrant, std::move(name), {}, std::move(message)};
}

namespace detail {

namespace {

// What a driver's open or close threw, in Rust's words.
std::string failure(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const DeviceFault& fault) {
        return std::string("panicked: ") + fault.what();
    } catch (const std::exception& e) {
        return e.what();
    } catch (const char* text) {
        return std::string("panicked: ") + text;
    } catch (...) {
        return "panicked: unknown panic";
    }
}

void name_thread([[maybe_unused]] const std::string& name) {
#ifdef __linux__
    // Linux keeps 15 bytes of a thread's name.
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#endif
}

}  // namespace

DeviceCore::DeviceCore(Factory factory, DeviceOptions options)
    : factory_(std::move(factory)), options_(std::move(options)) {
    if (options_.mailbox == 0) {
        options_.mailbox = 1;
    }
}

std::string DeviceCore::name() const {
    std::lock_guard lock(mutex_);
    return name_.empty() ? "device" : name_;
}

DeviceState DeviceCore::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

std::size_t DeviceCore::queue_depth() const {
    std::lock_guard lock(mutex_);
    return mailbox_.size();
}

void DeviceCore::open(std::string_view name) {
    std::shared_ptr<std::promise<void>> opened;
    {
        std::lock_guard lock(mutex_);
        if (name_.empty()) {
            name_ = std::string(name);
        }
        if (factory_taken_) {
            throw DeviceError::open(std::string(name), "the device has already been opened");
        }
        factory_taken_ = true;
        state_ = DeviceState::opening;
        accepting_ = true;
        opened = std::make_shared<std::promise<void>>();
    }
    auto result = opened->get_future();
    std::thread thread([self = shared_from_this(), opened] { self->run(opened); });
    {
        std::lock_guard lock(mutex_);
        thread_id_ = thread.get_id();
        thread_ = std::move(thread);
    }
    result.get();
}

void DeviceCore::create() {
    // On the device thread.
    std::string name;
    {
        std::lock_guard lock(mutex_);
        name = name_;
    }
    try {
        auto driver = factory_();
        driver->open();
        auto abort = driver->abort_handle();
        driver_ = std::move(driver);
        std::lock_guard lock(mutex_);
        abort_ = std::move(abort);
        state_ = DeviceState::ready;
    } catch (...) {
        const auto source = failure(std::current_exception());
        {
            std::lock_guard lock(mutex_);
            state_ = DeviceState::closed;
        }
        throw DeviceError::open(name, source);
    }
}

void DeviceCore::run(std::shared_ptr<std::promise<void>> opened) {
    std::string name = this->name();
    this_device_thread = "device:" + name;
    name_thread(this_device_thread);
    if (options_.thread_init) {
        options_.thread_init();
    }
    try {
        create();
        opened->set_value();
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            accepting_ = false;
        }
        opened->set_exception(std::current_exception());
        return;
    }

    while (true) {
        Item item;
        {
            std::unique_lock lock(mutex_);
            has_items_.wait(lock, [&] { return !mailbox_.empty(); });
            item = std::move(mailbox_.front());
            mailbox_.pop_front();
        }
        has_room_.notify_one();
        switch (item.control) {
            case Control::none: {
                if (item.job.abandoned && item.job.abandoned->load()) {
                    continue;
                }
                DeviceState state;
                std::string reason;
                {
                    std::lock_guard lock(mutex_);
                    state = state_;
                    reason = reason_;
                }
                if (state == DeviceState::ready && driver_) {
                    if (auto panic = item.job.run(driver_->get())) {
                        log::emit({log::Level::error,
                                   "device call panicked: " + *panic + " device=" + name,
                                   __FILE__,
                                   __LINE__,
                                   std::chrono::system_clock::now(),
                                   {},
                                   {}});
                        {
                            std::lock_guard lock(mutex_);
                            state_ = DeviceState::faulted;
                            reason_ = *panic;
                        }
                        item.job.fail(DeviceError::panicked(name, *panic));
                    }
                } else if (state == DeviceState::faulted) {
                    item.job.fail(DeviceError::faulted(name, reason));
                } else {
                    item.job.fail(DeviceError::not_open(name));
                }
                break;
            }
            case Control::reset: {
                if (driver_) {
                    try {
                        driver_->close();
                    } catch (...) {
                        log::warn("closing the driver for a reset failed: {} device={}",
                                  failure(std::current_exception()), name);
                    }
                    driver_.reset();
                }
                {
                    std::lock_guard lock(mutex_);
                    state_ = DeviceState::opening;
                    reason_.clear();
                }
                try {
                    create();
                    item.reply->set_value();
                } catch (...) {
                    item.reply->set_exception(std::current_exception());
                }
                break;
            }
            case Control::close: {
                {
                    std::lock_guard lock(mutex_);
                    state_ = DeviceState::closing;
                }
                std::exception_ptr error;
                if (driver_) {
                    try {
                        driver_->close();
                    } catch (...) {
                        error =
                            std::make_exception_ptr(DeviceError::driver(failure(std::current_exception())));
                    }
                    driver_.reset();
                }
                {
                    std::lock_guard lock(mutex_);
                    abort_ = nullptr;
                    state_ = DeviceState::closed;
                }
                if (error) {
                    item.reply->set_exception(error);
                } else {
                    item.reply->set_value();
                }
                return;
            }
        }
    }
}

void DeviceCore::check_callable() const {
    std::lock_guard lock(mutex_);
    const std::string name = name_.empty() ? "device" : name_;
    if (thread_.joinable() && std::this_thread::get_id() == thread_id_) {
        throw DeviceError::reentrant(name);
    }
    switch (state_) {
        case DeviceState::faulted:
            throw DeviceError::faulted(name, reason_);
        case DeviceState::ready:
        case DeviceState::opening:
            break;
        default:
            throw DeviceError::not_open(name);
    }
}

void DeviceCore::push(Item item) {
    std::unique_lock lock(mutex_);
    has_room_.wait(lock, [&] { return !accepting_ || mailbox_.size() < options_.mailbox; });
    if (!accepting_) {
        throw DeviceError::not_open(name_.empty() ? "device" : name_);
    }
    mailbox_.push_back(std::move(item));
    lock.unlock();
    has_items_.notify_one();
}

void DeviceCore::enqueue(Job job) {
    Item item;
    item.job = std::move(job);
    push(std::move(item));
}

void DeviceCore::reset() {
    Item item;
    item.control = Control::reset;
    item.reply = std::make_shared<std::promise<void>>();
    auto answer = item.reply->get_future();
    push(std::move(item));
    answer.get();
}

void DeviceCore::close() {
    auto reply = std::make_shared<std::promise<void>>();
    auto answer = reply->get_future();
    {
        std::unique_lock lock(mutex_);
        // Room for the close request, unless the device is already closed.
        has_room_.wait(lock, [&] { return !accepting_ || mailbox_.size() < options_.mailbox; });
        if (!accepting_) {
            // Never opened, already closed, or the open failed.
            lock.unlock();
            if (thread_.joinable() && std::this_thread::get_id() != thread_id_) {
                thread_.join();
            }
            return;
        }
        Item item;
        item.control = Control::close;
        item.reply = reply;
        mailbox_.push_back(std::move(item));
        // Nothing is queued after the close request.
        accepting_ = false;
    }
    has_items_.notify_one();
    has_room_.notify_all();
    std::exception_ptr error;
    try {
        answer.get();
    } catch (...) {
        error = std::current_exception();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

bool DeviceCore::abort() {
    std::function<void()> abort;
    {
        std::lock_guard lock(mutex_);
        abort = abort_;
    }
    if (!abort) {
        return false;
    }
    abort();
    return true;
}

}  // namespace detail

}  // namespace teta::wot
