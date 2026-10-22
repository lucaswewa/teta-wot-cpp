#include "teta/wot/registry.hpp"

#include <algorithm>
#include <ranges>

namespace teta::wot {

std::string normalise_class_name(std::string_view name) {
    constexpr std::string_view space = " \t\r\n\f\v";
    const auto first = name.find_first_not_of(space);
    if (first == std::string_view::npos) {
        return {};
    }
    name = name.substr(first, name.find_last_not_of(space) - first + 1);
    std::string out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name.compare(i, 2, "::") == 0) {
            out += '.';
            ++i;
        } else {
            out += name[i] == ':' ? '.' : name[i];
        }
    }
    return out;
}

std::vector<std::string> ThingRegistry::names() const {
    std::vector<std::string> out;
    for (const auto& entry : entries_) {
        out.insert(out.end(), entry->names.begin(), entry->names.end());
    }
    return out;
}

const ThingRegistry::Entry* ThingRegistry::find(std::string_view cls) const {
    const auto wanted = normalise_class_name(cls);
    for (const auto& entry : std::views::reverse(entries_)) {
        if (std::ranges::find(entry->names, wanted) != entry->names.end()) {
            return entry.get();
        }
    }
    return nullptr;
}

bool ThingRegistry::add_thing(RuntimeBuilder& builder, const std::string& thing, std::string_view cls,
                              std::vector<json::Value> args, json::Value kwargs) const {
    const auto* entry = find(cls);
    if (!entry) {
        return false;
    }
    entry->add(builder, thing, std::move(args), std::move(kwargs));
    return true;
}

}  // namespace teta::wot
