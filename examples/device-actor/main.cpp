// A synchronous driver behind a device actor.
//
// The driver simulates a serial-port instrument. It isn't thread-safe, like
// many vendor SDK handles: it works because it is created, used and destroyed
// on its own thread. The example shows concurrent callers, a queued call
// whose future is collected later, a call timeout, a panic (DeviceFault) that
// faults the device, recovery with `reset`, and an out-of-band abort.
//
// The C++ port of teta-wot's `device-actor` example: it prints the same.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <teta/wot.hpp>
#include <thread>
#include <vector>

namespace tw = teta::wot;
using namespace std::chrono_literals;

/// A pretend serial port: every command takes 2 ms and is logged.
class SerialInstrument {
  public:
    explicit SerialInstrument(std::shared_ptr<std::atomic<bool>> busy) : busy_until_abort(std::move(busy)) {}

    std::string query(const std::string& command) {
        std::this_thread::sleep_for(2ms);
        transcript.push_back(command);
        return command + ": ok (#" + std::to_string(transcript.size()) + ")";
    }

    void open() { std::cout << "open on thread \"" << tw::device_thread_name() << "\"\n"; }
    void close() { std::cout << "close after " << transcript.size() << " commands\n"; }
    std::function<void()> abort_handle() const {
        return [flag = busy_until_abort] { flag->store(false); };
    }

    std::vector<std::string> transcript;
    std::shared_ptr<std::atomic<bool>> busy_until_abort;
};

void check(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

void run() {
    std::cout << std::unitbuf;  // each line as it is written
    auto busy = std::make_shared<std::atomic<bool>>(false);
    tw::DeviceOptions options;
    options.call_timeout = 200ms;
    // The factory runs on the device thread, so the driver never has to move.
    tw::Device<SerialInstrument> device([busy] { return SerialInstrument(busy); }, options);
    // A Thing's devices are opened by the runtime; here it is done by hand.
    device.open("example:serial");

    // 1. Many callers: the mailbox serialises them.
    std::vector<std::thread> callers;
    for (int i = 0; i < 20; ++i) {
        callers.emplace_back([&device, i] {
            device.call([i](SerialInstrument& d) { return d.query("READ? " + std::to_string(i)); });
        });
    }
    for (auto& caller : callers) {
        caller.join();
    }
    const auto count = device.call([](SerialInstrument& d) { return d.transcript.size(); });
    std::cout << "20 concurrent callers → " << count << " commands, one at a time\n";
    check(count == 20, "20 commands");

    // 2. A call queued now and collected later (`post` returns at once).
    auto pending = device.post([](SerialInstrument& d) { return d.query("*IDN?"); });
    std::cout << "from a plain thread: " << device.wait(pending) << "\n";

    // 3. A call that doesn't answer in time; an abort stops it out of band.
    busy->store(true);
    try {
        device.call([](SerialInstrument& d) {
            while (d.busy_until_abort->load()) {
                std::this_thread::sleep_for(5ms);
            }
            return std::string("unstuck");
        });
        throw std::runtime_error("the stuck call should time out");
    } catch (const tw::DeviceError& error) {
        std::cout << "stuck call: " << error.what() << "\n";
    }
    check(device.abort(), "the driver provides an abort handle");
    check(
        device.call([](SerialInstrument&) { return std::string("responsive again"); }) == "responsive again",
        "responsive again");

    // 4. A panic faults the device; later calls fail fast until a reset.
    try {
        device.call([](SerialInstrument&) { throw tw::DeviceFault("firmware bug"); });
    } catch (const tw::DeviceError& error) {
        std::cerr << "panicking call: " << error.what() << "\n";
    }
    check(device.state() == tw::DeviceState::faulted, "faulted");
    try {
        device.call([](SerialInstrument& d) { return d.query("*IDN?"); });
    } catch (const tw::DeviceError& error) {
        std::cout << "next call: " << error.what() << "\n";
    }
    device.reset();
    check(device.state() == tw::DeviceState::ready, "ready after the reset");
    std::cout << "after reset: " << device.call([](SerialInstrument& d) { return d.query("*IDN?"); }) << "\n";
    device.close();
}

int main() {
    try {
        run();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
