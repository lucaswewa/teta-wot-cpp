// Arrays: teta-wot-core's `array.rs` tests (nested lists, numpy's message for
// ragged lists, a fixed rank checked, the schemas), and the array itself.

#include "teta/wot/ndarray.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "teta/wot.hpp"

namespace tw = teta::wot;
using tw::json::Value;

namespace ndarray_test {

#define EXPECT_SAME(a, b) EXPECT_EQ(tw::json::dump(a), tw::json::dump(b))

template <typename A>
A decode(const char* text) {
    return tw::Codec<A>::decode(tw::json::parse(text));
}

std::string decode_error(const char* text) {
    try {
        (void)decode<tw::NdArray<>>(text);
    } catch (const tw::ValueDecodeError& error) {
        return error.message();
    }
    return "<decoded>";
}

}  // namespace ndarray_test

using namespace ndarray_test;

TEST(NdArray, ArraysAreNestedLists) {
    const auto matrix = tw::NdArray<std::int64_t, 2>::from_shape({2, 3}, {1, 2, 3, 4, 5, 6});
    EXPECT_EQ(tw::json::dump(tw::Codec<std::remove_cvref_t<decltype(matrix)>>::encode(matrix)),
              "[[1,2,3],[4,5,6]]");
    const auto scalar = tw::NdArray<double, 0>::from_shape({}, {2.5});
    EXPECT_EQ(tw::json::dump(tw::Codec<std::remove_cvref_t<decltype(scalar)>>::encode(scalar)), "2.5");
    const tw::NdArray<> empty;
    EXPECT_EQ(tw::json::dump(tw::Codec<tw::NdArray<>>::encode(empty)), "[]");
    EXPECT_EQ(empty.shape(), (std::vector<std::size_t>{0}));
    EXPECT_EQ((tw::NdArray<double, 2>().shape()), (std::vector<std::size_t>{0, 0}));

    EXPECT_EQ((decode<tw::NdArray<std::int64_t, 2>>("[[1, 2, 3], [4, 5, 6]]")), matrix);
    const auto dynamic = decode<tw::NdArray<>>("[[[1.5]]]");
    EXPECT_EQ(dynamic.shape(), (std::vector<std::size_t>{1, 1, 1}));
    const auto zero = decode<tw::NdArray<>>("5");
    EXPECT_EQ(zero.ndim(), 0u);
    EXPECT_EQ(zero.elements()[0], 5.0);
    EXPECT_EQ(decode<tw::NdArray<>>("[[], []]").shape(), (std::vector<std::size_t>{2, 0}));
    EXPECT_EQ((decode<tw::NdArray<double, 2>>("[]").shape()), (std::vector<std::size_t>{0, 0}))
        << "an empty list can't show its inner dimensions";
    EXPECT_EQ(tw::json::dump(tw::Codec<tw::NdArray<>>::encode(decode<tw::NdArray<>>("[[1, 2], [3, 4]]"))),
              "[[1.0,2.0],[3.0,4.0]]")
        << "doubles as serde writes them";
}

TEST(NdArray, RaggedListsGetNumpysMessage) {
    for (const auto& [text, after, detected] : std::vector<std::tuple<const char*, int, const char*>>{
             {"[[1, 2], [3]]", 1, "(2,)"},
             {"[1, [2]]", 1, "(2,)"},
             {"[[[1, 2], [3]], [[4, 5], [6, 7]]]", 2, "(2, 2)"},
             {"[[1, 2], [3, [4]]]", 2, "(2, 2)"},
             {"[[1], [[2]]]", 2, "(2, 1)"}}) {
        EXPECT_EQ(decode_error(text),
                  "setting an array element with a sequence. The requested array has an "
                  "inhomogeneous shape after " +
                      std::to_string(after) + " dimensions. The detected shape was " + detected +
                      " + inhomogeneous part.")
            << text;
    }
}

TEST(NdArray, AFixedRankIsChecked) {
    try {
        (void)decode<tw::NdArray<double, 1>>("[[1.0]]");
        ADD_FAILURE();
    } catch (const tw::ValueDecodeError& error) {
        EXPECT_EQ(error.message(), "expected an array of 1 dimensions, got 2");
    }
    EXPECT_THROW((void)decode<tw::NdArray<>>("[\"a\"]"), tw::DecodeError)
        << "elements are the element type's";
}

TEST(NdArray, Schemas) {
    const auto dynamic = tw::Codec<tw::NdArray<>>::json_schema();
    EXPECT_EQ(dynamic["title"], "NestedListOfNumbersModel");
    EXPECT_EQ(dynamic["anyOf"].size(), 9u);
    EXPECT_SAME(dynamic["anyOf"][8]["items"]["items"]["items"]["items"]["items"]["items"]["items"],
                Value::object());
    const auto fixed = tw::Codec<tw::NdArray<double, 2>>::json_schema();
    EXPECT_EQ(fixed["type"], "array");
    EXPECT_EQ(fixed["items"]["items"]["type"], "number");
    // As a property, the root `title` goes, as teta-wot's `for_type` drops
    // schemars' root title.
    EXPECT_FALSE(tw::td::to_json(tw::for_type<tw::NdArray<>>()).contains("title"));
}

TEST(NdArray, TheArray) {
    auto cube = tw::NdArray<std::int64_t, 3>::zeros({2, 3, 4});
    EXPECT_EQ(cube.size(), 24u);
    cube.at({1, 2, 3}) = 7;
    EXPECT_EQ(cube.elements().back(), 7) << "row-major";
    EXPECT_THROW((void)cube.at({2, 0, 0}), std::out_of_range);
    EXPECT_THROW((void)cube.at({0, 0}), std::out_of_range);
    EXPECT_EQ(cube.sum(), 7);
    const auto doubled = cube.map([](std::int64_t v) { return static_cast<double>(v) * 0.5; });
    EXPECT_EQ(doubled.at({1, 2, 3}), 3.5);
    EXPECT_THROW((void)(tw::NdArray<double, 2>::from_shape({2}, {1.0, 2.0})), std::invalid_argument)
        << "the rank";
    EXPECT_THROW((void)(tw::NdArray<>::from_shape({2, 2}, {1.0})), std::invalid_argument) << "the size";
    EXPECT_EQ((tw::NdArray<double, 0>().size()), 1u) << "a zero-dimensional array has its one element";
}
