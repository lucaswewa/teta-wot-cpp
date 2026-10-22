// A property's getter takes nothing: it is read, not called with arguments.
#include <cstdint>
#include <teta/wot.hpp>

namespace tw = teta::wot;

struct Thing {
    std::int64_t level(std::int64_t channel) const { return channel; }
    static void define(tw::ThingSpec<Thing>& t) { t.property("level", &Thing::level); }  // error
};

int main() {
    return 0;
}
