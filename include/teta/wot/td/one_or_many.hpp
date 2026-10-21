// A TD term that may be written as one value or as an array of values.

#pragma once

#include <utility>
#include <variant>
#include <vector>

namespace teta::wot::td {

/// One value, or any number of values as an array. The two forms are kept
/// apart, so a TD is written back exactly as it was built.
template <typename T>
class OneOrMany {
  public:
    OneOrMany(T value) : value_(std::move(value)) {}
    OneOrMany(std::vector<T> values) : value_(std::move(values)) {}

    /// Whether this is the array form.
    bool is_many() const { return std::holds_alternative<std::vector<T>>(value_); }

    /// The values, as a vector (one element for the single form).
    std::vector<T> values() const {
        if (is_many()) {
            return std::get<std::vector<T>>(value_);
        }
        return {std::get<T>(value_)};
    }

    const T& one() const { return std::get<T>(value_); }
    const std::vector<T>& many() const { return std::get<std::vector<T>>(value_); }

    bool contains(const T& value) const {
        for (const auto& v : values()) {
            if (v == value) {
                return true;
            }
        }
        return false;
    }

    bool operator==(const OneOrMany&) const = default;

  private:
    std::variant<T, std::vector<T>> value_;
};

}  // namespace teta::wot::td
