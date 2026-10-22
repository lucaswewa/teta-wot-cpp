// A slot's Thing type (or interface) must be complete where the slot is
// declared: Things that refer to each other connect one way through an
// interface.
#include <teta/wot.hpp>

namespace tw = teta::wot;

class Camera;

class Autofocus {
  public:
    tw::Slot<Camera> camera;  // error: Camera isn't complete yet
    static void define(tw::ThingSpec<Autofocus>& t) { t.slot("camera", &Autofocus::camera); }
};

class Camera {
  public:
    static void define(tw::ThingSpec<Camera>&) {}
};

int main() {
    (void)tw::Runtime::builder().thing<Autofocus>("autofocus").thing<Camera>("camera").build();
    return 0;
}
