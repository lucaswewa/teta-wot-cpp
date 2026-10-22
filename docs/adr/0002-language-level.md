# ADR-0002: Language level: C++20

- **Status:** Accepted (2026-10-21, with the plan)
- **Plan decision:** D2

## Context

The goals ask for modern C++ on Windows and Linux. C++23 adds `std::expected` and a few conveniences, but MSVC, GCC and Clang ship different parts of it; C++20 is complete on all three.

## Decision

- **C++20**, as compiled by MSVC 17.10+, GCC 13+ and Clang 17+ (`CMAKE_CXX_STANDARD 20`, no extensions).
- Used freely: concepts, `std::jthread` and `std::stop_token`, `std::format`, `std::source_location`, `std::span`, ranges, class-type non-type template parameters, designated initialisers, `std::to_chars` for doubles.
- Not used: C++23, and coroutines (ADR-0005). `tl::expected` (vcpkg `tl-expected`) stands in for `std::expected`, as `teta::wot::Expected`, from the phase that first needs it (Phase 3).

## Consequences

- A clang-cl CI job besides MSVC catches portability issues early.
- Moving to C++23 later is a minor change: `Expected` can become an alias of `std::expected`.
