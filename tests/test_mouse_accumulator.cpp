// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone checks of the mouse accumulator behind gow3gpu_mouse_read: no window or SDL video needed.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "gpu/shim/tsuki_mouse.h"

using Gow3Mouse::MouseAccumulator;
using Gow3Mouse::kPush;

struct Read {
    int32_t dx, dy, wheel;
    uint32_t buttons;
};
static Read Take(MouseAccumulator& m, bool quiet = false) {
    Read r;
    m.Take(&r.dx, &r.dy, &r.wheel, &r.buttons, quiet);
    return r;
}
// First read after the pointer is seen: the game's cursor goes to the origin, the next read to the pointer.
static void Sync(MouseAccumulator& m, float x, float y) {
    m.Position(x, y, 1920, 1080);
    Read r = Take(m);
    assert(r.dx == -kPush && r.dy == -kPush);
}

static void no_motion_no_delta() {
    MouseAccumulator m;
    Read r = Take(m);
    assert(r.dx == 0 && r.dy == 0); // the game keeps its own cursor until the pointer moves
    r = Take(m);
    assert(r.dx == 0 && r.dy == 0);
}

static void first_read_pushes_to_origin() {
    MouseAccumulator m;
    Sync(m, 960, 540);
    Read r = Take(m);
    assert(r.dx == 320 && r.dy == 224); // the window centre is the centre of 640x448
    r = Take(m);
    assert(r.dx == 0 && r.dy == 0);
}

static void follows_position() {
    MouseAccumulator m;
    Sync(m, 960, 540);
    Take(m);
    m.Position(1440, 270, 1920, 1080); // (480, 112)
    Read r = Take(m);
    assert(r.dx == 160 && r.dy == -112);
    m.Position(302.9f, 270, 1920, 1080); // 100.97 -> cell 100
    r = Take(m);
    assert(r.dx == -380 && r.dy == 0);
    m.Position(720, 135, 960, 540); // a smaller window: the same place
    r = Take(m);
    assert(r.dx == 380 && r.dy == 0);
    m.Position(5, 5, 0, 1080); // no size: ignored
    r = Take(m);
    assert(r.dx == 0 && r.dy == 0);
}

static void edges_resync() {
    MouseAccumulator m;
    Sync(m, 960, 540);
    Take(m);
    m.Position(0, 1079.9f, 1920, 1080); // cell (0, 447): both edges
    Read r = Take(m);
    assert(r.dx == -kPush && r.dy == kPush); // the game clamps, so any drift is gone here
    m.Position(0.5f, 1079.5f, 1920, 1080); // still on the edge: nothing, so a pointer parked there
    r = Take(m);                           // does not take the selection from the pad
    assert(r.dx == 0 && r.dy == 0);
    m.Position(3, 1077, 1920, 1080); // cell (1, 446)
    r = Take(m);
    assert(r.dx == 1 && r.dy == -1);
    m.Position(-50, 540, 1920, 1080); // outside the window: clamped
    r = Take(m);
    assert(r.dx == -kPush);
    m.Position(2000, 540, 1920, 1080);
    r = Take(m);
    assert(r.dx == kPush);
}

static void menu_is_quiet() {
    MouseAccumulator m;
    Sync(m, 960, 540);
    Take(m); // cursor at (320, 224)
    m.Wheel(2);
    m.Button(SDL_BUTTON_LEFT, true);
    m.Position(1440, 270, 1920, 1080);
    Read r = Take(m, true);
    assert(r.dx == 0 && r.dy == 0 && r.wheel == 0 && r.buttons == 0); // the menu owns the mouse
    r = Take(m);
    assert(r.dx == 160 && r.dy == -112); // when it closes, the cursor goes to the pointer
    assert(r.wheel == 0);                // what scrolled the menu does not scroll the game
    assert(r.buttons == 0x1);            // the real button state
}

static void take_resets() {
    MouseAccumulator m;
    m.Wheel(1);
    Read r = Take(m);
    assert(r.wheel == 1);
    r = Take(m);
    assert(r.wheel == 0);
}

static void buttons_are_state() {
    MouseAccumulator m;
    m.Button(SDL_BUTTON_LEFT, true); m.Button(SDL_BUTTON_X2, true);
    assert(Take(m).buttons == 0x11);
    m.Button(SDL_BUTTON_LEFT, false);
    assert(Take(m).buttons == 0x10); // Take does not clear them
    m.Button(SDL_BUTTON_RIGHT, true); m.Button(SDL_BUTTON_MIDDLE, true); m.Button(SDL_BUTTON_X1, true);
    assert(Take(m).buttons == (0x10 | 0x2 | 0x4 | 0x8));
}

static void wheel_adds_up() {
    MouseAccumulator m;
    m.Wheel(0.5f); m.Wheel(1); m.Wheel(1);
    assert(Take(m).wheel == 2); // whole notches; the half stays
    m.Wheel(0.5f);
    assert(Take(m).wheel == 1);
}

static void null_outputs() {
    MouseAccumulator m;
    m.Position(960, 540, 1920, 1080);
    m.Take(nullptr, nullptr, nullptr, nullptr, false); // the push to the origin
    int32_t dx;
    m.Take(&dx, nullptr, nullptr, nullptr, false);
    assert(dx == 320);
}

int main() {
    no_motion_no_delta(); first_read_pushes_to_origin(); follows_position(); edges_resync(); menu_is_quiet();
    take_resets(); buttons_are_state(); wheel_adds_up(); null_outputs();
    puts("mouse accumulator test: ok");
    return 0;
}
