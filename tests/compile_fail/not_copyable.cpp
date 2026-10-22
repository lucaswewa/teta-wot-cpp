// A property value must be copyable: readers get a copy.
#include <memory>
#include <teta/wot.hpp>

namespace tw = teta::wot;

struct Thing {
    tw::Prop<std::unique_ptr<int>> value;  // error: not a PropertyValue
    static void define(tw::ThingSpec<Thing>& t) { t.property("value", &Thing::value); }
};

int main() {
    return 0;
}
