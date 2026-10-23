// n-dimensional arrays on the wire: a port of teta-wot-core's `array`.
//
// `NdArray<T, Rank>` holds a shape and its elements in row-major order, and is
// written as nested lists of its elements, one level per dimension: a matrix
// is `[[1.0, 2.0], [3.0, 4.0]]`, and a zero-dimensional array is its one
// element. It is read back from the same form.
//
// Schemas. With a fixed rank, it is exactly that many levels of arrays of
// the element's schema; with `dynamic_rank` (the default), LabThings'
// `NestedListOfNumbersModel`: a number, or lists of numbers up to six deep, or
// seven levels of lists of anything.
//
// Validation. Nested lists whose lengths differ at some depth, or that mix
// numbers and lists there, are refused with numpy's message, as pydantic
// reports it: "Value error, setting an array element with a sequence. The
// requested array has an inhomogeneous shape after 1 dimensions. The detected
// shape was (2,) + inhomogeneous part." Elements must match the element
// type's schema.

#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "teta/wot/codec.hpp"

namespace teta::wot {

/// The rank of an array whose number of dimensions isn't fixed.
inline constexpr std::size_t dynamic_rank = std::numeric_limits<std::size_t>::max();

/// An n-dimensional array, carried as nested lists (see above).
template <typename T = double, std::size_t Rank = dynamic_rank>
class NdArray {
  public:
    /// An empty array: no elements, with the rank's number of axes (one for
    /// `dynamic_rank`; a zero-dimensional array has its one element).
    NdArray() : shape_(Rank == dynamic_rank ? 1 : Rank, 0) {
        if (Rank == 0) {
            elements_.resize(1);
        }
    }

    /// An array of this shape and these elements, in row-major order.
    /// Throws std::invalid_argument if they don't match, or the rank isn't
    /// `Rank`.
    static NdArray from_shape(std::vector<std::size_t> shape, std::vector<T> elements) {
        if (Rank != dynamic_rank && shape.size() != Rank) {
            throw std::invalid_argument("expected an array of " + std::to_string(Rank) + " dimensions, got " +
                                        std::to_string(shape.size()));
        }
        if (product(shape) != elements.size()) {
            throw std::invalid_argument("the shape doesn't match the number of elements");
        }
        NdArray out;
        out.shape_ = std::move(shape);
        out.elements_ = std::move(elements);
        return out;
    }
    /// An array of this shape, every element `value`.
    static NdArray filled(std::vector<std::size_t> shape, const T& value) {
        const auto count = product(shape);
        return from_shape(std::move(shape), std::vector<T>(count, value));
    }
    /// An array of this shape, every element zero (`T{}`).
    static NdArray zeros(std::vector<std::size_t> shape) { return filled(std::move(shape), T{}); }

    const std::vector<std::size_t>& shape() const { return shape_; }
    std::size_t ndim() const { return shape_.size(); }
    /// The number of elements.
    std::size_t size() const { return elements_.size(); }
    /// The elements, in row-major order.
    std::span<const T> elements() const { return elements_; }
    std::span<T> elements() { return elements_; }

    /// The element at these indices (one per dimension). Throws
    /// std::out_of_range.
    const T& at(std::span<const std::size_t> indices) const { return elements_[offset(indices)]; }
    T& at(std::span<const std::size_t> indices) { return elements_[offset(indices)]; }
    const T& at(std::initializer_list<std::size_t> indices) const {
        return at(std::span(indices.begin(), indices.size()));
    }
    T& at(std::initializer_list<std::size_t> indices) {
        return at(std::span(indices.begin(), indices.size()));
    }

    /// The same shape, each element `f(element)`.
    template <typename F>
    auto map(F f) const {
        using U = std::invoke_result_t<F, const T&>;
        std::vector<U> mapped;
        mapped.reserve(elements_.size());
        for (const auto& element : elements_) {
            mapped.push_back(f(element));
        }
        return NdArray<U, Rank>::from_shape(shape_, std::move(mapped));
    }
    /// The sum of the elements.
    T sum() const { return std::accumulate(elements_.begin(), elements_.end(), T{}); }

    bool operator==(const NdArray&) const = default;

  private:
    template <typename, std::size_t>
    friend class NdArray;

    static std::size_t product(const std::vector<std::size_t>& shape) {
        return std::accumulate(shape.begin(), shape.end(), std::size_t{1}, std::multiplies<>());
    }

    std::size_t offset(std::span<const std::size_t> indices) const {
        if (indices.size() != shape_.size()) {
            throw std::out_of_range("one index per dimension");
        }
        std::size_t at = 0;
        for (std::size_t axis = 0; axis < shape_.size(); ++axis) {
            if (indices[axis] >= shape_[axis]) {
                throw std::out_of_range("index out of the array");
            }
            at = at * shape_[axis] + indices[axis];
        }
        return at;
    }

    std::vector<std::size_t> shape_;
    std::vector<T> elements_;
};

namespace ndarray_detail {

/// The description of the `NestedListOfNumbersModel` schema.
inline constexpr std::string_view nested_list_description =
    "A RootModel describing a list-of-lists up to 7 deep.\n\nThis is used to generate a JSONSchema "
    "description of a "
    "`numpy.ndarray`\nserialised to a list. It is used in the annotated `.NDArray` type.";

/// LabThings' schema for an array of any rank.
json::Value nested_list_schema();

/// The shape of nested lists; throws ValueDecodeError with numpy's message
/// when they aren't a rectangular array (at each depth, every item must be a
/// list of one length, or none may be a list).
std::vector<std::size_t> shape_of(const json::Value& value);

/// The elements of rectangular nested lists, in row-major order.
void flatten(const json::Value& value, std::vector<const json::Value*>& out);

}  // namespace ndarray_detail

template <typename T, std::size_t Rank>
struct Codec<NdArray<T, Rank>> {
    static constexpr bool referenced = false;

    /// schemars' name for the schema (a root `title` that is only this is
    /// dropped, as teta-wot's `DataSchema::for_type` drops it).
    static std::string schema_name()
        requires(Rank == dynamic_rank)
    {
        return "NestedListOfNumbersModel";
    }

    static json::Value json_schema() {
        if constexpr (Rank == dynamic_rank) {
            return ndarray_detail::nested_list_schema();
        } else {
            json::Value schema = Codec<T>::json_schema();
            for (std::size_t i = 0; i < Rank; ++i) {
                json::Value level = json::Value::object();
                level["type"] = "array";
                level["items"] = std::move(schema);
                schema = std::move(level);
            }
            return schema;
        }
    }

    static json::Value encode(const NdArray<T, Rank>& array) {
        const auto& shape = array.shape();
        const auto elements = array.elements();
        if (shape.empty()) {
            return elements.empty() ? json::Value(nullptr) : Codec<T>::encode(elements.front());
        }
        std::size_t next = 0;
        return nested(shape, 0, elements, next);
    }

    static NdArray<T, Rank> decode(const json::Value& value) {
        auto shape = ndarray_detail::shape_of(value);
        // An empty list can't show its inner dimensions: `[]` is a 0 x 0
        // matrix for a fixed rank of 2.
        if (Rank != dynamic_rank && shape.size() < Rank && !shape.empty() && shape.back() == 0) {
            shape.resize(Rank, 0);
        }
        std::vector<const json::Value*> flat;
        ndarray_detail::flatten(value, flat);
        std::vector<T> elements;
        elements.reserve(flat.size());
        for (const auto* element : flat) {
            elements.push_back(Codec<T>::decode(*element));
        }
        if (Rank != dynamic_rank && shape.size() != Rank) {
            throw ValueDecodeError("expected an array of " + std::to_string(Rank) + " dimensions, got " +
                                   std::to_string(shape.size()));
        }
        return NdArray<T, Rank>::from_shape(std::move(shape), std::move(elements));
    }

  private:
    static json::Value nested(const std::vector<std::size_t>& shape, std::size_t axis,
                              std::span<const T> elements, std::size_t& next) {
        json::Value list = json::Value::array();
        for (std::size_t i = 0; i < shape[axis]; ++i) {
            if (axis + 1 == shape.size()) {
                list.push_back(Codec<T>::encode(elements[next++]));
            } else {
                list.push_back(nested(shape, axis + 1, elements, next));
            }
        }
        return list;
    }
};

}  // namespace teta::wot
