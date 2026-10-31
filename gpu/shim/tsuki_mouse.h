// SPDX-License-Identifier: GPL-2.0-or-later
// tsuki: mouse state for libSceMouse. The window thread feeds SDL events in; the guest thread takes
// them out through tsukigpu_mouse_read. Header only and free of ImGui/Vulkan so a test can use it.

#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>
#include <SDL3/SDL_mouse.h>

namespace TsukiMouse {

/// The game's cursor space: it adds the reported deltas to its own cursor and clamps it to
/// 0..0x27f and 0..0x1bf (docs/specs/2026-10-14-spec-6-2d-raton-saves.md §4). A push larger than
/// the space lands that cursor on an edge wherever it was.
inline constexpr int32_t kWidth = 640, kHeight = 448, kPush = 1024;

struct MouseAccumulator {
    /// Pointer position in window coordinates. The window maps onto the game's space, so the game's
    /// cursor sits where the pointer is and both reach an edge together (with deltas taken 1:1 it
    /// crossed the space in a third of the window and stopped at its edge, Spec 6 task 6).
    void Position(float x, float y, float width, float height) {
        if (!(width > 0) || !(height > 0)) return;
        std::scoped_lock lock{mutex};
        target_x = Cell(x * kWidth / width, kWidth);
        target_y = Cell(y * kHeight / height, kHeight);
        seen = true;
    }
    void Wheel(float ywheel) {
        std::scoped_lock lock{mutex};
        w += ywheel;
    }
    /// Held state, kept across Take. Game bits: 0x1 left, 0x2 right, 0x4 middle, 0x8/0x10 SDL X1/X2.
    void Button(uint8_t sdl_button, bool down) {
        const uint32_t bit = BitOf(sdl_button);
        std::scoped_lock lock{mutex};
        held = down ? held | bit : held & ~bit;
    }
    /// The deltas that move the game's cursor to the pointer since the last call. Where that cursor
    /// starts is unknown, so the first read after the pointer is seen pushes it to the origin; arriving
    /// on an edge pushes it against that edge, which clears any drift. The wheel is whole notches (the
    /// fraction stays). quiet (a menu owns the mouse): nothing moves or is pressed for the game and the
    /// wheel is dropped; the cursor goes to the pointer when the menu closes. NULL outputs are skipped.
    void Take(int32_t* dx, int32_t* dy, int32_t* wheel, uint32_t* buttons, bool quiet) {
        std::scoped_lock lock{mutex};
        int32_t mx = 0, my = 0, notches = 0;
        uint32_t pressed = 0;
        if (quiet) {
            w = 0;
        } else {
            if (synced) {
                mx = Step(target_x, x, kWidth);
                my = Step(target_y, y, kHeight);
            } else if (seen) {
                mx = my = -kPush;
                x = y = 0;
                synced = true;
            }
            notches = int32_t(std::trunc(w));
            w -= float(notches);
            pressed = held;
        }
        if (dx) *dx = mx;
        if (dy) *dy = my;
        if (wheel) *wheel = notches;
        if (buttons) *buttons = pressed;
    }

private:
    static int32_t Cell(float v, int32_t size) {
        const float c = std::floor(v);
        return !(c > 0) ? 0 : c >= float(size - 1) ? size - 1 : int32_t(c);
    }
    static int32_t Step(int32_t target, int32_t& at, int32_t size) {
        if (target == at) return 0;
        const int32_t delta = target == 0 ? -kPush : target == size - 1 ? kPush : target - at;
        at = target;
        return delta;
    }
    static uint32_t BitOf(uint8_t sdl_button) {
        switch (sdl_button) {
        case SDL_BUTTON_LEFT: return 0x1;
        case SDL_BUTTON_RIGHT: return 0x2;
        case SDL_BUTTON_MIDDLE: return 0x4;
        case SDL_BUTTON_X1: return 0x8;
        case SDL_BUTTON_X2: return 0x10;
        default: return 0;
        }
    }
    std::mutex mutex;
    int32_t target_x = 0, target_y = 0, x = 0, y = 0; // the pointer's cell and where the game's cursor is
    bool seen = false, synced = false;
    float w = 0;
    uint32_t held = 0;
};

} // namespace TsukiMouse
