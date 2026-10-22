// A property whose type has no codec: it can't be described or read.
#include <teta/wot.hpp>

namespace tw = teta::wot;

struct Opaque {
    Opaque() = default;
    explicit Opaque(int) {}  // not an aggregate: the codecs can't see its fields
    int secret = 0;
};

struct Thing {
    tw::Prop<Opaque> value;  // error: Opaque isn't a PropertyValue
    static void define(tw::ThingSpec<Thing>& t) { t.property("value", &Thing::value); }
};

int main() {
    return 0;
}
