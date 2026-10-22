// A Thing has `static void define(tw::ThingSpec<T>&)`.
#include <teta/wot.hpp>

namespace tw = teta::wot;

struct Lamp {
    tw::Prop<bool> on{false};
};

int main() {
    (void)tw::Runtime::builder().thing<Lamp>("lamp").build();  // error: Lamp isn't a ThingType
    return 0;
}
