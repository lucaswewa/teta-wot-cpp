// The Thing types a server can build from a configuration file: a port of
// teta-wot-server's `registry`.
//
//     tw::ThingRegistry registry;
//     registry.add<Counter>("my_lab.counters:Counter")
//         .add<Stage>({"my_lab.stages:Stage", "stages:Stage"});
//
// A configuration file names a Thing's type by its `cls` (Python's import
// string). `a.b:C`, `a.b.C` and `a::b::C` are the same name, and every type is
// also registered under its short C++ name (`Counter`).

#pragma once

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "teta/wot/runtime.hpp"

namespace teta::wot {

/// A class name as the registry compares it: `:` and `::` become `.`, and
/// surrounding whitespace goes.
std::string normalise_class_name(std::string_view name);

/// The Thing types a server can build from configuration, by name.
class ThingRegistry {
  public:
    /// Registers `T` under `name`, and under its short C++ type name.
    template <FromConfig T>
    ThingRegistry& add(std::string_view name) {
        return add<T>({name});
    }
    /// Registers `T` under several names, and under its short C++ type name.
    /// A name registered twice is the last registration's, so applications
    /// can override a library's.
    template <FromConfig T>
    ThingRegistry& add(std::initializer_list<std::string_view> names) {
        auto entry = std::make_shared<Entry>();
        for (const auto name : names) {
            entry->names.push_back(normalise_class_name(name));
        }
        if (auto short_name = codec::short_name(typeid(T));
            std::ranges::find(entry->names, short_name) == entry->names.end()) {
            entry->names.push_back(std::move(short_name));
        }
        entry->add = [](RuntimeBuilder& builder, const std::string& thing, std::vector<json::Value> args,
                        json::Value kwargs) {
            builder.thing_from_args<T>(thing, std::move(args), std::move(kwargs));
        };
        entries_.push_back(std::move(entry));
        return *this;
    }

    /// Whether a class name (import string) is registered.
    bool contains(std::string_view cls) const { return find(cls) != nullptr; }
    /// Every registered name, in registration order.
    std::vector<std::string> names() const;

    /// Adds the Thing named `thing`, of class `cls`, to a runtime builder;
    /// false if the class isn't registered.
    bool add_thing(RuntimeBuilder& builder, const std::string& thing, std::string_view cls,
                   std::vector<json::Value> args, json::Value kwargs) const;

  private:
    struct Entry {
        std::vector<std::string> names;
        std::function<void(RuntimeBuilder&, const std::string&, std::vector<json::Value>, json::Value)> add;
    };
    const Entry* find(std::string_view cls) const;

    std::vector<std::shared_ptr<const Entry>> entries_;
};

}  // namespace teta::wot
