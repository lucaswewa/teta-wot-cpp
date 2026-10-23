// libFuzzer's entry point: an exception that escapes is a crash it reports.

#include <cstddef>
#include <cstdint>

#include "fuzz.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    fuzz::one_input({reinterpret_cast<const char*>(data), size});
    return 0;
}
