// Host checks for direct/runtime/input_state.hpp: key sources, HSP stick bits
// and the scripted test input.
#include "input_state.hpp"
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

int main() {
    using namespace cp2::input;
    KeyState k;
    check(!k.down(13), "keys start up");
    k.set(kTouch, 13, true);
    k.set(kScript, 13, true);
    k.set(kTouch, 13, false);
    check(k.down(13), "a key held by another source stays down");
    k.clear(kScript);
    check(!k.down(13), "clearing the last source releases it");
    check(!k.down(-1) && !k.down(256), "out-of-range keys are up");

    k.set(kTouch, 37, true);   // left
    k.set(kTouch, 40, true);   // down
    k.set(kTouch, 13, true);   // Enter
    k.set(kTouch, 27, true);   // Esc
    k.set(kTouch, 90, true);   // Z has no stick bit
    check(k.stick_bits() == (1 | 8 | 32 | 128), "stick bits use hgio_stick's Windows layout");

    InputScript s;
    std::string error;
    check(s.parse("40@10-12,13@20-21", error), "script parses");
    KeyState t;
    s.apply(t, 9);
    check(!t.down(40), "before a press");
    s.apply(t, 10);
    check(t.down(40) && !t.down(13), "press starts at FROM");
    s.apply(t, 12);
    check(!t.down(40), "press ends before TO");
    s.apply(t, 20);
    check(t.down(13), "second press");
    check(!s.parse("13@5-5", error) && s.empty(), "empty range rejected");
    check(!s.parse("13@5", error), "missing range rejected");
    check(!s.parse("300@1-2", error), "key out of range rejected");
    check(s.parse(nullptr, error) && s.empty(), "no script");

    std::printf("input state: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
