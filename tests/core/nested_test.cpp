// Nested exceptions, step by step (clang-cl crashed on them in CI: these
// tests say where).

#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <string>

#include "teta/wot/action.hpp"

namespace {

std::exception_ptr nested() {
    try {
        try {
            throw std::runtime_error("inner");
        } catch (...) {
            std::throw_with_nested(std::runtime_error("outer"));
        }
    } catch (...) {
        return std::current_exception();
    }
    return nullptr;
}

TEST(Nested, CatchTheOuter) {
    try {
        std::rethrow_exception(nested());
    } catch (const std::exception& e) {
        EXPECT_STREQ(e.what(), "outer");
    }
}

TEST(Nested, FindTheNestedException) {
    try {
        std::rethrow_exception(nested());
    } catch (const std::exception& e) {
        const auto* n = dynamic_cast<const std::nested_exception*>(&e);
        ASSERT_NE(n, nullptr);
        EXPECT_TRUE(n->nested_ptr());
    }
}

TEST(Nested, RethrowTheInner) {
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
        EXPECT_EQ(teta::wot::detail::message_chain(e), "outer: inner");
        EXPECT_EQ(teta::wot::detail::exception_title(e), "Error");
    }
}

}  // namespace
