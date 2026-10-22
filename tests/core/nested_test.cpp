// Nested exceptions, step by step (clang-cl crashed on them in CI: these
// tests say where).

#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <string>

#include "teta/wot/action.hpp"

namespace {

std::exception_ptr nested() {
    std::exception_ptr out;
    try {
        try {
            throw std::runtime_error("inner");
        } catch (...) {
            std::throw_with_nested(std::runtime_error("outer"));
        }
    } catch (...) {
        out = std::current_exception();
    }
    return out;
}

// Whether `std::throw_with_nested` captures the exception being handled
// (clang-cl's doesn't: the nested exception is null).
bool captures_nested() {
    try {
        std::rethrow_exception(nested());
    } catch (const std::nested_exception& n) {
        return static_cast<bool>(n.nested_ptr());
    } catch (...) {
    }
    return false;
}

TEST(Nested, CatchTheOuter) {
    try {
        std::rethrow_exception(nested());
    } catch (const std::exception& e) {
        EXPECT_STREQ(e.what(), "outer");
    }
}

TEST(Nested, FindTheNestedException) {
    if (!captures_nested()) {
        GTEST_SKIP() << "std::throw_with_nested captures nothing with this compiler";
    }
    try {
        std::rethrow_exception(nested());
    } catch (const std::exception& e) {
        const auto* n = dynamic_cast<const std::nested_exception*>(&e);
        ASSERT_NE(n, nullptr);
        EXPECT_TRUE(n->nested_ptr());
    }
}

TEST(Nested, RethrowTheInner) {
    if (!captures_nested()) {
        GTEST_SKIP() << "std::throw_with_nested captures nothing with this compiler";
    }
    try {
        std::rethrow_exception(nested());
    } catch (const std::exception& e) {
        try {
            std::rethrow_if_nested(e);
        } catch (const std::exception& inner) {
            EXPECT_STREQ(inner.what(), "inner");
        }
    }
}

TEST(Nested, ChainAndTitle) {
    try {
        std::rethrow_exception(nested());
    } catch (const std::exception& e) {
        // Without the nested exception, the chain is the outer one alone.
        EXPECT_EQ(teta::wot::detail::message_chain(e), captures_nested() ? "outer: inner" : "outer");
        EXPECT_EQ(teta::wot::detail::exception_title(e), "Error");
    }
}

}  // namespace
