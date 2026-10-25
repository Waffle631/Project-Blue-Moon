// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: mouse state for libSceMouse. The window thread feeds SDL events in; the guest thread takes
// them out through gow3gpu_mouse_read. Header only and free of ImGui/Vulkan so a test can use it.

#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>
#include <SDL3/SDL_mouse.h>

namespace Gow3Mouse {

struct MouseAccumulator {
    /// Relative motion in SDL units (float: HiDPI and slow movements are fractions).
    void Motion(float xrel, float yrel) {
        std::scoped_lock lock{mutex};
        x += xrel;
        y += yrel;
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
    /// Returns what accumulated since the last call. Whole units only (truncated toward zero): the
    /// fraction stays for the next call, so many small movements are not lost. NULL outputs are skipped.
    void Take(int32_t* dx, int32_t* dy, int32_t* wheel, uint32_t* buttons) {
        std::scoped_lock lock{mutex};
        const float tx = std::trunc(x), ty = std::trunc(y), tw = std::trunc(w);
        x -= tx;
        y -= ty;
        w -= tw;
        if (dx) *dx = int32_t(tx);
        if (dy) *dy = int32_t(ty);
        if (wheel) *wheel = int32_t(tw);
        if (buttons) *buttons = held;
    }

private:
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
    float x = 0, y = 0, w = 0;
    uint32_t held = 0;
};

} // namespace Gow3Mouse
