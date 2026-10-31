// SPDX-License-Identifier: GPL-2.0-or-later
#include "tsuki_settings.h"
#include "tsuki_graphics.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace TsukiSettings {

namespace {

const char* Path() {
    const char* env = std::getenv("TSUKI_CONFIG");
    return env && env[0] ? env : "tsuki.ini";
}

namespace P = TsukiPerfStats;

/// The overlay's items in tsuki.ini. An old overlay_upscaler key (Spec 7 §10) is ignored, like the
/// deleted temporal upscaler's keys (§12).
constexpr std::pair<const char*, unsigned> OverlayKeys[] = {
    {"overlay_fps", P::Fps}, {"overlay_frametime", P::FrameTime}, {"overlay_gpu", P::Gpu},
    {"overlay_cpu", P::Cpu}, {"overlay_ram", P::Ram},             {"overlay_vram", P::Vram},
};

void Set(Values& v, const std::string& key, const std::string& value) {
    const int i = std::atoi(value.c_str());
    for (const auto& [name, bit] : OverlayKeys) {
        if (key == name) {
            v.overlay_items = i ? (v.overlay_items | bit) : (v.overlay_items & ~bit);
            return;
        }
    }
    if (key == "overlay_scale") {
        v.overlay_scale = std::clamp(i, 50, 300);
        return;
    }
    if (key == "overlay_corner") {
        v.overlay_corner = i >= 0 && i <= 3 ? i : 1;
        return;
    }
    if (key == "overlay_opacity") {
        v.overlay_opacity = std::clamp(i, 0, 100);
        return;
    }
    if (key == "overlay_layout") {
        v.overlay_layout = i == 1 ? 1 : 0;
        return;
    }
    if (key == "menu_tab") {
        v.menu_tab = i >= 0 && i <= 2 ? i : 0; // tsuki_overlay.cpp: Image, Performance, Overlay
        return;
    }
    if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "display_mode") {
        v.display_mode = TsukiGraphics::ParseDisplayMode(value);
    } else if (key == "vsync") {
        v.vsync = i != 0;
    } else if (key == "fps_limit") {
        v.fps_limit = TsukiGraphics::ParseFpsLimit(i);
    } else if (key == "async_shaders") {
        v.async_shaders = i != 0;
    } else if (key == "fsr1") {
        v.fsr1 = i != 0;
    } else if (key == "rcas") {
        v.rcas = i != 0;
    } else if (key == "rcas_strength") {
        v.rcas_strength = TsukiGraphics::ParseRcasStrength(value.empty() ? -1 : i);
    } else if (key == "frames_queued") {
        v.frames_queued = TsukiGraphics::ParseFramesQueued(value.empty() ? -1 : i);
    } else if (key == "deferred_readback") {
        v.deferred_readback = i != 0;
    } else if (key == "stale_readback") {
        v.stale_readback = i != 0;
    } else if (key == "restart_unconfirmed") {
        v.restart_unconfirmed = i != 0;
    }
}

} // namespace

Values& Get() {
    static Values values;
    return values;
}

void Load() {
    auto& v = Get();
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            Set(v, text.substr(0, eq), text.substr(eq + 1));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
    }
    // Environment overrides (scripts, A/B tests).
    const std::pair<const char*, const char*> env_keys[] = {
        {"TSUKI_ASYNC_SHADERS", "async_shaders"},      {"TSUKI_DEFERRED_READBACK", "deferred_readback"},
        {"TSUKI_STALE_READBACK", "stale_readback"},     {"TSUKI_FSR1", "fsr1"},
        {"TSUKI_RCAS", "rcas"},                        {"TSUKI_FRAMES_AHEAD", "frames_queued"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env); value && value[0]) {
            Set(v, key, value);
        }
    }
    v.startup_deferred_readback = v.deferred_readback;
    v.startup_stale_readback = v.stale_readback;
}

bool GraphicsNeedRestart() {
    const auto& v = Get();
    return v.deferred_readback != v.startup_deferred_readback ||
           v.stale_readback != v.startup_stale_readback;
}

void Save() {
    const auto& v = Get();
    FILE* file = std::fopen(Path(), "w");
    if (!file) {
        std::printf("Settings: cannot write %s\n", Path());
        return;
    }
    std::fprintf(file, "# tsuki settings (in-game menu: Insert / R3+L2)\nshow_fps=%d\n", int(v.show_fps.load()));
    for (const auto& [name, bit] : OverlayKeys) {
        std::fprintf(file, "%s=%d\n", name, (v.overlay_items & bit) ? 1 : 0);
    }
    std::fprintf(file, "overlay_scale=%d\noverlay_corner=%d\noverlay_opacity=%d\noverlay_layout=%d\nmenu_tab=%d\n",
                 v.overlay_scale.load(), v.overlay_corner.load(), v.overlay_opacity.load(),
                 v.overlay_layout.load(), v.menu_tab.load());
    std::fprintf(file,
                 "display_mode=%s\nvsync=%d\nfps_limit=%d\nasync_shaders=%d\n"
                 "fsr1=%d\nrcas=%d\nrcas_strength=%d\nframes_queued=%d\n"
                 "deferred_readback=%d\nstale_readback=%d\n"
                 "restart_unconfirmed=%d\n",
                 TsukiGraphics::DisplayModeKeys[std::clamp(v.display_mode.load(), 0, 2)].data(),
                 int(v.vsync.load()), v.fps_limit.load(), int(v.async_shaders.load()),
                 int(v.fsr1.load()), int(v.rcas.load()), v.rcas_strength.load(), v.frames_queued.load(),
                 int(v.deferred_readback.load()), int(v.stale_readback.load()),
                 int(v.restart_unconfirmed.load()));
    std::fclose(file);
}

} // namespace TsukiSettings
