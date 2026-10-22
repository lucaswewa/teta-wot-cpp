// Every data parameter of an action is named, in order.
#include <cstdint>
#include <teta/wot.hpp>

namespace tw = teta::wot;

struct Stage {
    void move_to(std::int64_t x, std::int64_t y) { (void)x, (void)y; }
    static void define(tw::ThingSpec<Stage>& t) {
        t.action<"x">("move_to", &Stage::move_to);  // error: two parameters, one name
    }
};

int main() {
    (void)tw::Runtime::builder().thing<Stage>("stage").build();
    return 0;
}
