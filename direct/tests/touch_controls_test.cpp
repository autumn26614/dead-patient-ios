// Host checks for direct/runtime/touch_controls.hpp: layout inside the view
// and safe area, joystick directions, knob travel and dead zone, buttons,
// multi-touch and release.
#include "touch_controls.hpp"
#include <cmath>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

int main() {
    using namespace cp2;
    touch::Controls c;
    check(!c.touch(1, 100, 100, 0), "no layout: touches go to the game");
    // An iPhone 16 Pro in landscape: 2622 x 1206 pixels, 177 px side insets.
    c.layout(2622, 1206, 177, 177, 63);
    const touch::Stick& pad = c.stick();
    check(pad.x - pad.radius >= 177 && pad.y + pad.radius <= 1206 - 63, "stick inside the safe area");
    check(pad.knob_x == pad.x && pad.knob_y == pad.y && !pad.held, "knob centred at rest");
    for (const touch::Button& b : c.buttons())
        check(b.x + b.radius <= 2622 - 177 && b.y + b.radius <= 1206 && b.y - b.radius >= 0, "button inside the view");
    const input::KeyState& keys = input::keys();

    check(!c.touch(2, 1311, 300, 0), "a touch in the middle goes to the game");
    check(c.touch(3, pad.x + pad.radius * 0.8f, pad.y, 0) && keys.down(39) && !keys.down(37), "pad right");
    c.touch(3, pad.x, pad.y - pad.radius * 0.8f, 1);
    check(keys.down(38) && !keys.down(39), "dragging the pad turns up");
    c.touch(3, pad.x + pad.radius * 0.6f, pad.y + pad.radius * 0.6f, 1);
    check(keys.down(39) && keys.down(40), "diagonal down-right holds two arrows");
    c.touch(3, pad.x + 1, pad.y + 1, 1);
    check(!keys.down(37) && !keys.down(38) && !keys.down(39) && !keys.down(40), "dead zone releases the arrows");
    c.touch(3, pad.x + pad.radius * 3, pad.y, 1);
    check(keys.down(39), "a stick touch dragged outside keeps steering");
    check(pad.held && std::fabs(pad.knob_x - (pad.x + touch::Controls::kTravel * pad.radius)) < 0.01f &&
              pad.knob_y == pad.y, "the knob stops at its travel");
    c.touch(3, pad.x - pad.radius * 0.3f, pad.y, 1);
    check(keys.down(37) && std::fabs(pad.knob_x - (pad.x - pad.radius * 0.3f)) < 0.01f, "the knob follows the thumb");

    const touch::Button ok = c.buttons()[0];
    check(c.touch(4, ok.x, ok.y, 0) && keys.down(88) && keys.down(37), "OK (X) with the stick held");
    c.touch(4, ok.x + ok.radius * 2, ok.y, 1);
    check(!keys.down(88), "a button touch dragged away lets go");
    c.touch(4, ok.x, ok.y, 1);
    check(keys.down(88), "dragging back presses again");
    c.touch(4, ok.x, ok.y, 2);
    check(!keys.down(88) && keys.down(37), "ending the button touch keeps the stick");
    c.touch(3, 0, 0, 2);
    check(!keys.down(37) && !keys.down(39) && !pad.held && pad.knob_x == pad.x, "ending the stick touch recentres");

    static const int expected[] = {88, 67, 90, 83, 27};
    for (int i = 0; i < 5; ++i) {
        const touch::Button b = c.buttons()[static_cast<std::size_t>(i)];
        c.touch(10 + i, b.x, b.y, 0);
        check(keys.down(expected[i]), "each button holds its key");
        c.touch(10 + i, b.x, b.y, 2);
        check(!keys.down(expected[i]), "and releases it");
    }
    c.touch(20, c.buttons()[1].x, c.buttons()[1].y, 0);
    c.release_all();
    check(!keys.down(67), "release_all lifts everything");
    check(!c.touch(99, 5, 5, 1), "moves of unknown touches go to the game");

    c.touch(30, c.buttons()[0].x, c.buttons()[0].y, 0);
    c.set_hidden(true);
    check(!keys.down(88), "hiding the controls releases their keys");
    check(!c.touch(31, c.buttons()[0].x, c.buttons()[0].y, 0) && !keys.down(88), "hidden controls pass touches to the game");
    c.set_hidden(false);
    check(c.touch(32, c.buttons()[0].x, c.buttons()[0].y, 0) && keys.down(88), "shown again, they work");
    c.release_all();

    std::printf("touch controls: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
