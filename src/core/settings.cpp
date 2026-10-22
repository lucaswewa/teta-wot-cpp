#include "teta/wot/settings.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#include "teta/wot/log.hpp"
#include "teta/wot/property.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace teta::wot {

std::string to_settings_json(const json::Value& value) {
    return json::dump_pretty(value, 4);
}

namespace detail {

std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path path_from_text(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

namespace {

// An error as the Rust standard library describes an I/O error.
std::string describe(const std::error_code& error) {
    return error.message() + " (os error " + std::to_string(error.value()) + ")";
}

std::error_code last_error() {
    return {errno, std::generic_category()};
}

// Writes `contents` to `path` (a new file), and flushes it to the disk.
std::error_code write_synced(const std::filesystem::path& path, const std::string& contents) {
#ifdef _WIN32
    std::FILE* file = nullptr;
    if (const auto failed = _wfopen_s(&file, path.c_str(), L"wb"); failed != 0) {
        return {failed, std::generic_category()};
    }
#else
    std::FILE* file = std::fopen(path.c_str(), "wb");
#endif
    if (!file) {
        return last_error();
    }
    std::error_code error;
    if (std::fwrite(contents.data(), 1, contents.size(), file) != contents.size() || std::fflush(file) != 0) {
        error = last_error();
    }
#ifdef _WIN32
    if (!error && _commit(_fileno(file)) != 0) {
        error = last_error();
    }
#else
    if (!error && fsync(fileno(file)) != 0) {
        error = last_error();
    }
#endif
    if (std::fclose(file) != 0 && !error) {
        error = last_error();
    }
    return error;
}

// Writes a file by writing a temporary file next to it and renaming it over
// the target, so readers never see a partial file.
std::error_code write_atomically(const std::filesystem::path& path, const std::string& contents) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return error;
        }
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    if (error = write_synced(temporary, contents); error) {
        return error;
    }
    std::filesystem::rename(temporary, path, error);
    return error;
}

}  // namespace

SettingsStore::SettingsStore(const std::filesystem::path& folder, std::string thing, std::string class_name)
    : thing_(std::move(thing)),
      class_name_(std::move(class_name)),
      path_(folder / thing_ / ("Settings-" + class_name_ + ".json")) {}

void SettingsStore::add(const std::string& name, std::shared_ptr<SettingAccess> access) {
    std::lock_guard lock(mutex_);
    const auto at =
        std::ranges::lower_bound(settings_, name, {}, [](const auto& entry) { return entry.first; });
    settings_.emplace(at, name, std::move(access));
}

bool SettingsStore::has_settings() const {
    std::lock_guard lock(mutex_);
    return !settings_.empty();
}

SettingsStore::Entries SettingsStore::entries() const {
    std::lock_guard lock(mutex_);
    return settings_;
}

void SettingsStore::save() const {
    {
        std::lock_guard lock(mutex_);
        if (!enabled_) {
            return;
        }
    }
    std::lock_guard writing(writing_);
    json::Value values = json::Value::object();
    for (const auto& [name, access] : entries()) {
        if (auto value = access->current()) {
            values[name] = std::move(*value);
        }
    }
    std::string text;
    try {
        text = to_settings_json(values);
    } catch (const std::exception& error) {
        throw PropertyError::failed(error.what());
    }
#ifdef _WIN32
    // Written in text mode, as Python writes it: CRLF on Windows. JSON strings
    // can't hold raw line ends, so this changes only the layout.
    std::string crlf;
    crlf.reserve(text.size() + text.size() / 8);
    for (const char c : text) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    text = std::move(crlf);
#endif
    if (const auto error = write_atomically(path_, text)) {
        throw PropertyError::failed("couldn't save the settings of `" + thing_ + "` to " + path_text(path_) +
                                    ": " + describe(error));
    }
}

void SettingsStore::load() {
    const auto accesses = entries();
    for (const auto& [name, access] : accesses) {
        access->prepare();
    }
    if (const auto settings = read()) {
        for (const auto& [name, value] : settings->items()) {
            const auto it = std::ranges::find(accesses, name, [](const auto& entry) { return entry.first; });
            if (it == accesses.end()) {
                log::warn(
                    "An extra key {} was found in the settings file. It will be deleted the next time "
                    "settings are "
                    "saved.",
                    name);
                continue;
            }
            try {
                it->second->load(value);
            } catch (const std::exception& error) {
                log::warn("Could not load setting {} from settings file because of a validation error: {}",
                          name, error.what());
            }
        }
    }
    std::lock_guard lock(mutex_);
    enabled_ = true;
}

std::optional<json::Value> SettingsStore::read() const {
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) {
        return std::nullopt;
    }
    std::string problem;
    json::Value parsed;
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        problem = describe(last_error());
    } else {
        const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        try {
            parsed = json::parse(bytes);
        } catch (const json::ParseError& parse_error) {
            problem = parse_error.what();
        }
    }
    if (!problem.empty()) {
        log::warn(
            "Error loading settings for {} from {}, could not load a JSON object. Settings for this Thing "
            "will be "
            "reset to default. ({})",
            class_name_, path_text(path_), problem);
        return std::nullopt;
    }
    if (!parsed.is_object()) {
        log::warn("Error loading settings for {} from {}. The file does not contain a Mapping", class_name_,
                  path_text(path_));
        return std::nullopt;
    }
    return parsed;
}

}  // namespace detail

}  // namespace teta::wot
