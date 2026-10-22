// An action parameter is read from the request: its type needs a codec.
#include <teta/wot.hpp>

namespace tw = teta::wot;

class Handle {
  public:
    explicit Handle(int fd) : fd_(fd) {}

  private:
    int fd_;
};

struct Thing {
    void use(Handle handle) { (void)handle; }
    static void define(tw::ThingSpec<Thing>& t) { t.action<"handle">("use", &Thing::use); }  // error
};

int main() {
    (void)tw::Runtime::builder().thing<Thing>("thing").build();
    return 0;
}
