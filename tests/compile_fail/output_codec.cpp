// An action's output is sent back as JSON: its type needs a codec.
#include <teta/wot.hpp>

namespace tw = teta::wot;

class Image {
  public:
    explicit Image(int width) : width_(width) {}

  private:
    int width_;
};

struct Camera {
    Image capture() { return Image(640); }
    static void define(tw::ThingSpec<Camera>& t) { t.action("capture", &Camera::capture); }  // error
};

int main() {
    (void)tw::Runtime::builder().thing<Camera>("camera").build();
    return 0;
}
