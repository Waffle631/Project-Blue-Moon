// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone checks of the mouse accumulator behind gow3gpu_mouse_read: no window or SDL video needed.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "gpu/shim/gow3_mouse.h"

using Gow3Mouse::MouseAccumulator;

static void fractions_are_kept() {
    MouseAccumulator m;
    int32_t dx, dy, wheel; uint32_t buttons;
    m.Motion(3, -2); m.Motion(3, -2); m.Motion(0.6f, 0.6f);
    m.Take(&dx, &dy, &wheel, &buttons);
    assert(dx == 6 && dy == -3); // 6.6 and -3.4 truncate toward zero
    m.Motion(0.5f, 0.5f); // the rest (0.6, -0.4) plus this one: (1.1, 0.1)
    m.Take(&dx, &dy, &wheel, &buttons);
    assert(dx == 1 && dy == 0);
}

static void take_resets() {
    MouseAccumulator m;
    int32_t dx, dy, wheel; uint32_t buttons;
    m.Motion(4, 5); m.Wheel(1);
    m.Take(&dx, &dy, &wheel, &buttons);
    assert(dx == 4 && dy == 5 && wheel == 1);
    m.Take(&dx, &dy, &wheel, &buttons);
    assert(dx == 0 && dy == 0 && wheel == 0);
}

static void buttons_are_state() {
    MouseAccumulator m;
    int32_t dx, dy, wheel; uint32_t buttons;
    m.Button(SDL_BUTTON_LEFT, true); m.Button(SDL_BUTTON_X2, true);
    m.Take(&dx, &dy, &wheel, &buttons);
    assert(buttons == 0x11);
    m.Button(SDL_BUTTON_LEFT, false);
    m.Take(&dx, &dy, &wheel, &buttons); // Take does not clear them
    assert(buttons == 0x10);
    m.Button(SDL_BUTTON_RIGHT, true); m.Button(SDL_BUTTON_MIDDLE, true); m.Button(SDL_BUTTON_X1, true);
    m.Take(&dx, &dy, &wheel, &buttons);
    assert(buttons == (0x10 | 0x2 | 0x4 | 0x8));
}

static void wheel_adds_up() {
    MouseAccumulator m;
    int32_t dx, dy, wheel; uint32_t buttons;
    m.Wheel(1); m.Wheel(1);
    m.Take(&dx, &dy, &wheel, &buttons);
    assert(wheel == 2);
}

static void null_outputs() {
    MouseAccumulator m;
    m.Motion(1, 1);
    m.Take(nullptr, nullptr, nullptr, nullptr);
    int32_t dx; m.Take(&dx, nullptr, nullptr, nullptr);
    assert(dx == 0); // the first Take already consumed it
}

int main() {
    fractions_are_kept(); take_resets(); buttons_are_state(); wheel_adds_up(); null_outputs();
    puts("mouse accumulator test: ok");
    return 0;
}
