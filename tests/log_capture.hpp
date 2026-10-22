// The messages logged while a test runs, at WARN and above.

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "teta/wot/log.hpp"

namespace testing_support {

class LogCapture {
  public:
    LogCapture() {
        teta::wot::log::set_sink(
            [this](const teta::wot::log::Event& event) {
                std::lock_guard lock(mutex_);
                messages_.push_back(event.message);
            },
            teta::wot::log::Level::warn);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    ~LogCapture() { teta::wot::log::set_sink(nullptr); }

    std::vector<std::string> messages() const {
        std::lock_guard lock(mutex_);
        return messages_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
};

}  // namespace testing_support
