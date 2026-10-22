// UUIDs: the IDs of invocations (and, from Phase 7, Blobs).

#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace teta::wot {

/// A UUID, written in lower-case hyphenated form.
class Uuid {
  public:
    /// The nil UUID.
    Uuid() = default;
    explicit Uuid(const std::array<unsigned char, 16>& bytes) : bytes_(bytes) {}

    /// A random (version 4) UUID.
    static Uuid v4();
    /// Reads the hyphenated form (either case); `nullopt` if it isn't one.
    static std::optional<Uuid> parse(std::string_view text);

    std::string to_string() const;
    bool is_nil() const;
    const std::array<unsigned char, 16>& bytes() const { return bytes_; }

    auto operator<=>(const Uuid&) const = default;

  private:
    std::array<unsigned char, 16> bytes_{};
};

}  // namespace teta::wot

template <>
struct std::hash<teta::wot::Uuid> {
    std::size_t operator()(const teta::wot::Uuid& uuid) const noexcept {
        std::size_t h = 0;
        for (unsigned char b : uuid.bytes()) {
            h = h * 131 + b;
        }
        return h;
    }
};
