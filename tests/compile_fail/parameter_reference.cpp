// An action parameter can't be a non-const reference: it is the request's.
#include <string>
#include <teta/wot.hpp>

namespace tw = teta::wot;

struct Thing {
    void rename(std::string& name) { name += "!"; }
    static void define(tw::ThingSpec<Thing>& t) { t.action<"name">("rename", &Thing::rename); }  // error
};

int main() {
    (void)tw::Runtime::builder().thing<Thing>("thing").build();
    return 0;
}
