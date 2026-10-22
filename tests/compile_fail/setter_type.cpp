// A setter takes a value of its property's type (the getter's).
#include <cstdint>
#include <string>
#include <teta/wot.hpp>

namespace tw = teta::wot;

struct Thing {
    std::int64_t level() const { return 0; }
    void set_level(std::string level) { (void)level; }
    static void define(tw::ThingSpec<Thing>& t) {
        t.property("level", &Thing::level).setter(&Thing::set_level);  // error: string, not int64
    }
};

int main() {
    return 0;
}
