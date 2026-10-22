// Settings: properties saved to disk. A port of teta-wot-core's `settings`.
//
// Each Thing with settings has a settings store for the file
// `{settings_folder}/{thing}/Settings-{Class}.json`. The file holds every
// setting, with keys in alphabetical order, as pydantic writes it with
// `model_dump_json(indent=4)`, so that files move freely between the Python,
// Rust and C++ servers.
//
// - Saving happens after every change: a data setting's `Prop::set` (from
//   C++ or a client), or a functional setting written through the property.
//   Files are written atomically (a temporary file, then a rename). Nothing is
//   saved before the file has been loaded once.
// - Loading happens when the runtime starts, before any Thing starts. A
//   missing file leaves the defaults. An unreadable file, one that isn't a
//   JSON object, an unknown key or an invalid value only logs a warning.

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "teta/wot/json.hpp"

namespace teta::wot {

/// JSON as pydantic's `model_dump_json(indent=4)` writes it: four-space
/// indentation, `": "` and `","` separators, UTF-8 kept, no trailing newline
/// (and `\n` line ends: the store writes CRLF on Windows).
std::string to_settings_json(const json::Value& value);

namespace detail {

/// A path as UTF-8 text, for messages (MSVC's `path::string()` throws for
/// characters outside the ANSI code page).
std::string path_text(const std::filesystem::path& path);
/// A path from UTF-8 text, such as a configuration file's (a `std::string`
/// is read in the ANSI code page on Windows).
std::filesystem::path path_from_text(std::string_view text);

/// Reads and loads one setting (internal).
class SettingAccess {
  public:
    virtual ~SettingAccess() = default;
    /// Prepares for saving, before the file is loaded (a functional setting
    /// reads its getter once).
    virtual void prepare() = 0;
    /// The value to save, if there is one.
    virtual std::optional<json::Value> current() const = 0;
    /// Sets a value read from the file, validated as a client's value.
    /// Throws PropertyError.
    virtual void load(const json::Value& value) = 0;
};

/// The settings file of one Thing (internal).
class SettingsStore {
  public:
    SettingsStore(const std::filesystem::path& folder, std::string thing, std::string class_name);

    const std::filesystem::path& path() const { return path_; }
    /// Adds a setting; settings are kept sorted by name.
    void add(const std::string& name, std::shared_ptr<SettingAccess> access);
    bool has_settings() const;

    /// Writes every setting to the file, if it has been loaded. Throws
    /// PropertyError.
    void save() const;
    /// Loads the file, then allows saving.
    void load();

  private:
    using Entries = std::vector<std::pair<std::string, std::shared_ptr<SettingAccess>>>;
    Entries entries() const;
    /// The file's settings, or nothing (with a warning if it exists but can't
    /// be used).
    std::optional<json::Value> read() const;

    std::string thing_;
    std::string class_name_;
    std::filesystem::path path_;
    mutable std::mutex mutex_;
    bool enabled_ = false;
    Entries settings_;
    mutable std::mutex writing_;
};

}  // namespace detail

}  // namespace teta::wot
