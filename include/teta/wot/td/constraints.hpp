// Value constraints on properties, and how they appear in a DataSchema.

#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "teta/wot/td/data_schema.hpp"

namespace teta::wot::td {

/// A numeric bound: any integer, or a finite double.
class Bound {
  public:
    template <std::integral T>
    Bound(T value) {
        if constexpr (std::is_signed_v<T>) {
            value_ = static_cast<std::int64_t>(value);
        } else if (value <= static_cast<std::uint64_t>(INT64_MAX)) {
            value_ = static_cast<std::int64_t>(value);
        } else {
            value_ = static_cast<std::uint64_t>(value);
        }
    }
    template <std::floating_point T>
    Bound(T value) : value_(static_cast<double>(value)) {}

    bool is_float() const { return std::holds_alternative<double>(value_); }
    double as_double() const;
    /// Whether the bound is zero or negative (a NaN is neither).
    bool non_positive() const;

    /// The bound as a JSON number; throws ConstraintError for a non-finite one.
    json::Value to_number(const char* constraint) const;

    bool operator==(const Bound&) const = default;

  private:
    std::variant<std::int64_t, std::uint64_t, double> value_;
};

/// Constraints can't be applied to a schema.
class ConstraintError : public std::runtime_error {
  public:
    enum class Kind {
        /// A constraint doesn't suit the schema's type.
        unsuitable,
        /// The schema has neither a `type` nor a `oneOf`.
        untyped,
        /// A float bound is infinite or NaN.
        not_finite,
        /// `multiple_of` is zero or negative.
        not_positive,
    };

    ConstraintError(Kind kind, std::string message) : std::runtime_error(std::move(message)), kind_(kind) {}
    Kind kind() const { return kind_; }

  private:
    Kind kind_;
};

/// The constraints on a property.
///
/// `apply` writes them into a DataSchema the way pydantic does: `ge` →
/// `minimum`, `gt` → `exclusiveMinimum`, `le` → `maximum`, `lt` →
/// `exclusiveMaximum`, `multiple_of` → `multipleOf`, `pattern` → `pattern`,
/// and `min_length`/`max_length` → `minLength`/`maxLength` for strings,
/// `minItems`/`maxItems` for arrays, or `minProperties`/`maxProperties` for
/// objects. `allow_inf_nan` only affects validation.
struct Constraints {
    std::optional<Bound> gt_;
    std::optional<Bound> ge_;
    std::optional<Bound> lt_;
    std::optional<Bound> le_;
    std::optional<Bound> multiple_of_;
    std::optional<bool> allow_inf_nan_;
    std::optional<std::uint64_t> min_length_;
    std::optional<std::uint64_t> max_length_;
    std::optional<std::string> pattern_;

    Constraints& gt(Bound value) { return gt_ = value, *this; }
    Constraints& ge(Bound value) { return ge_ = value, *this; }
    Constraints& lt(Bound value) { return lt_ = value, *this; }
    Constraints& le(Bound value) { return le_ = value, *this; }
    Constraints& multiple_of(Bound value) { return multiple_of_ = value, *this; }
    Constraints& allow_inf_nan(bool allow) { return allow_inf_nan_ = allow, *this; }
    Constraints& min_length(std::uint64_t length) { return min_length_ = length, *this; }
    Constraints& max_length(std::uint64_t length) { return max_length_ = length, *this; }
    Constraints& pattern(std::string regex) { return pattern_ = std::move(regex), *this; }

    /// Whether there are no constraints.
    bool empty() const { return *this == Constraints{}; }

    bool operator==(const Constraints&) const = default;

    /// Writes the constraints into `schema`.
    ///
    /// If `schema` has a `type`, every constraint must suit it. If it has no
    /// `type` but a `oneOf` (such as an optional integer), each constraint
    /// goes into every branch it suits, as pydantic does for `Optional`
    /// fields, and must suit at least one. Otherwise it throws.
    void apply(DataSchema& schema) const;
};

}  // namespace teta::wot::td
