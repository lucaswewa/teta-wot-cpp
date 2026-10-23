// What the fuzz targets share. Each target defines `fuzz::one_input`, and
// states its invariants with `fuzz::check`. Under libFuzzer (entry.cpp), a
// broken invariant is a crash (an uncaught exception); under the driver
// (driver.cpp), it is reported with the input that broke it.

#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace fuzz {

/// A broken invariant.
class Failure : public std::logic_error {
  public:
    using std::logic_error::logic_error;
};

/// Throws Failure with `what` unless `condition` holds.
inline void check(bool condition, const std::string& what) {
    if (!condition) {
        throw Failure(what);
    }
}

/// Runs one input (any bytes). Defined by each target.
void one_input(std::string_view input);

}  // namespace fuzz
