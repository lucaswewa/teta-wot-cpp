// A folder of its own for one test, removed afterwards.

#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#include "teta/wot/settings.hpp"
#include "teta/wot/uuid.hpp"

namespace testing_support {

class TemporaryFolder {
  public:
    TemporaryFolder()
        : path_(std::filesystem::temp_directory_path() /
                ("teta-wot-test-" + teta::wot::Uuid::v4().to_string())) {
        std::filesystem::create_directories(path_);
    }
    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;
    ~TemporaryFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }
    /// The path as UTF-8 text, for configurations.
    std::string text() const { return teta::wot::detail::path_text(path_); }

  private:
    std::filesystem::path path_;
};

}  // namespace testing_support
