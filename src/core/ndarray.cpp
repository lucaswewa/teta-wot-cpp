#include "teta/wot/ndarray.hpp"

#include <algorithm>

namespace teta::wot::ndarray_detail {

json::Value nested_list_schema() {
    json::Value branches = json::Value::array();
    branches.push_back(json::Value{{"type", "integer"}});
    branches.push_back(json::Value{{"type", "number"}});
    // A number, or lists of numbers up to six deep...
    json::Value nested = json::Value::object();
    nested["anyOf"] = json::Value::array({json::Value{{"type", "integer"}}, json::Value{{"type", "number"}}});
    for (int depth = 0; depth < 6; ++depth) {
        json::Value level = json::Value::object();
        level["items"] = std::move(nested);
        level["type"] = "array";
        nested = level;
        branches.push_back(std::move(level));
    }
    // ...or seven levels of lists of anything.
    json::Value anything = json::Value::object();
    for (int depth = 0; depth < 7; ++depth) {
        json::Value level = json::Value::object();
        level["items"] = std::move(anything);
        level["type"] = "array";
        anything = std::move(level);
    }
    branches.push_back(std::move(anything));
    json::Value schema = json::Value::object();
    schema["anyOf"] = std::move(branches);
    schema["description"] = std::string(nested_list_description);
    schema["title"] = "NestedListOfNumbersModel";
    return schema;
}

std::vector<std::size_t> shape_of(const json::Value& value) {
    std::vector<std::size_t> shape;
    std::vector<const json::Value*> level = {&value};
    for (;;) {
        std::vector<const json::Value*> lists;
        for (const auto* item : level) {
            if (item->is_array()) {
                lists.push_back(item);
            }
        }
        if (lists.empty()) {
            return shape;
        }
        const std::size_t length = lists.front()->size();
        const bool ragged = lists.size() != level.size() || std::ranges::any_of(lists, [&](const auto* list) {
                                return list->size() != length;
                            });
        if (ragged) {
            std::string detected = "(";
            for (std::size_t i = 0; i < shape.size(); ++i) {
                detected += (i ? ", " : "") + std::to_string(shape[i]);
            }
            detected += shape.size() == 1 ? ",)" : ")";
            throw ValueDecodeError(
                "setting an array element with a sequence. The requested array has an inhomogeneous shape "
                "after " +
                std::to_string(shape.size()) + " dimensions. The detected shape was " + detected +
                " + inhomogeneous part.");
        }
        shape.push_back(length);
        std::vector<const json::Value*> next;
        for (const auto* list : lists) {
            for (const auto& item : *list) {
                next.push_back(&item);
            }
        }
        level = std::move(next);
    }
}

void flatten(const json::Value& value, std::vector<const json::Value*>& out) {
    if (value.is_array()) {
        for (const auto& item : value) {
            flatten(item, out);
        }
    } else {
        out.push_back(&value);
    }
}

}  // namespace teta::wot::ndarray_detail
