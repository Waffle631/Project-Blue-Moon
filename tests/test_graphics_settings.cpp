// tsuki.ini graphics keys (tsuki_settings.cpp, tsuki_graphics.h).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "tsuki_graphics.h"
#include "tsuki_settings.h"
#ifndef _WIN32
#define _putenv_s(name, value) setenv(name, value, 1) // "" counts as unset for Load()
#endif

namespace G = TsukiGraphics;

// Spec 7 §12: the settings keep no field of the deleted temporal upscaler.
template <class T>
concept HasUpscaler = requires(T t) { t.upscaler; } || requires(T t) { t.preset; } ||
                      requires(T t) { t.output_res; } || requires(T t) { t.object_motion; };
static_assert(!HasUpscaler<TsukiSettings::Values>);
// Spec 8 §5: nor any of the render resolution and engine frame rate patches.
template <class T>
concept HasEngineRate = requires(T t) { t.render_resolution; } || requires(T t) { t.engine_fps; } ||
                        requires(T t) { t.applied_engine_fps; } || requires(T t) { t.startup_engine_fps; };
static_assert(!HasEngineRate<TsukiSettings::Values>);
// Spec 8 §5 (task 6b): nor the options Tsukihime gets nothing from (the frame rate limit, which only slowed
// the game down, and the GPU readback modes) and the "Apply and restart" that existed for the readbacks.
template <class T>
concept HasUnusedOption = requires(T t) { t.fps_limit; } || requires(T t) { t.deferred_readback; } ||
                          requires(T t) { t.stale_readback; } || requires(T t) { t.restart_unconfirmed; };
static_assert(!HasUnusedOption<TsukiSettings::Values>);

static std::string Read(const char* path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int main() {
    assert(G::ParseDisplayMode("borderless") == G::Borderless && G::ParseDisplayMode("bogus") == G::Windowed);
    assert(G::ParseFramesQueued(2) == 2 && G::ParseFramesQueued(0) == 0 && G::ParseFramesQueued(5) == 1);
    assert(G::ParseRcasStrength(100) == 100 && G::ParseRcasStrength(101) == 75 && G::ParseRcasStrength(-1) == 75);
    assert(G::RcasAttenuation(100) == 0.0f && G::RcasAttenuation(0) == 2.0f && G::RcasAttenuation(75) == 0.5f);
    assert(G::FsrPassRuns(true, false, 1280, 720, 2560, 1440) && !G::FsrPassRuns(false, false, 1280, 720, 2560, 1440));
    assert(G::FsrPassRuns(false, true, 1920, 1080, 1920, 1080) && !G::FsrPassRuns(true, false, 1920, 1080, 1920, 1080));
    assert(!G::FsrPassRuns(true, true, 2560, 1440, 1920, 1080));  // larger image: regular scaling

    const char* path = "graphics-settings-test.ini";
    {
        std::ofstream out(path);
        out << "display_mode=borderless\nvsync=0\nasync_shaders=0\n"
               "fsr1=1\nrcas=0\nrcas_strength=40\nframes_queued=2\n";
    }
    _putenv_s("TSUKI_CONFIG", path);
    _putenv_s("TSUKI_ASYNC_SHADERS", "");
    auto& v = TsukiSettings::Get();
    TsukiSettings::Load();
    assert(v.display_mode == G::Borderless && !v.vsync && !v.async_shaders);
    assert(v.fsr1 && !v.rcas && v.rcas_strength == 40 && v.frames_queued == 2);

    TsukiSettings::Save();
    const std::string saved = Read(path);
    for (const char* line : {"display_mode=borderless\n", "vsync=0\n", "async_shaders=0\n", "fsr1=1\n",
                             "rcas=0\n", "rcas_strength=40\n", "frames_queued=2\n"}) {
        assert(saved.find(line) != std::string::npos);
    }

    {
        std::ofstream out(path);
        out << "display_mode=huge\nrcas_strength=250\nframes_queued=9\n";
    }
    _putenv_s("TSUKI_ASYNC_SHADERS", "1");  // the environment still wins at start
    v.async_shaders = false;
    TsukiSettings::Load();
    assert(v.display_mode == G::Windowed);
    assert(v.rcas_strength == 75 && v.frames_queued == 1);
    assert(v.async_shaders);
    {
        // An old tsuki.ini with removed keys: ignored, and not written back.
        std::ofstream out(path);
        out << "cheat_health=1\nred_orb_multiplier=5.0\ndamage_taken=0.5\nenemy_health_bar=1\nvsync=0\n";
    }
    v.vsync = true;
    TsukiSettings::Load();
    assert(!v.vsync);
    TsukiSettings::Save();
    const std::string old = Read(path);
    for (const char* key : {"cheat_", "orb_multiplier", "damage_", "enemy_health_bar"}) {
        assert(old.find(key) == std::string::npos);
    }
    {
        // Spec 7 §12: the temporal upscaler is deleted. Its old keys are ignored and Save() does not
        // write them; FSR 1 is spatial and stays.
        std::ofstream out(path);
        out << "upscaler=fsr3\npreset=2\njitter=0\nreactive=1\nobject_motion=0\nreactive_scale=4\n"
               "reactive_threshold=0.5\nreactive_max=0.5\ndebug_view=2\noutput_res=2560x1440\nsharpen=0\n"
               "sharpness=1.5\nfsr4_auto_exposure=0\nfsr4_invert_jitter=1\nlive_resolution=1\n"
               "overlay_upscaler=1\nfsr1=1\nrcas=0\nrcas_strength=40\n";
    }
    v.fsr1 = false;
    TsukiSettings::Load();
    assert(v.fsr1 && !v.rcas && v.rcas_strength == 40 && !(v.overlay_items & 1u << 2));
    TsukiSettings::Save();
    const std::string temporal = Read(path);
    for (const char* key : {"upscaler", "preset=", "jitter", "reactive", "object_motion", "debug_view", "output_res",
                            "sharpen", "sharpness", "fsr4_", "live_resolution"}) {
        assert(temporal.find(key) == std::string::npos);
    }
    assert(temporal.find("fsr1=1\n") != std::string::npos && temporal.find("rcas=0\n") != std::string::npos);
    assert(temporal.find("rcas_strength=40\n") != std::string::npos);
    {
        // Spec 8 §5: the render resolution and the engine frame rate were patches of the previous game, and
        // task 6b took out the frame rate limit, the readback modes and their restart. An old tsuki.ini with
        // them (and run.py's old TSUKI_ENGINE_FPS) changes nothing, and Save() drops them.
        std::ofstream out(path);
        out << "render_resolution=4K\nengine_fps=240\nfps_limit=30\ndeferred_readback=0\nstale_readback=0\n"
               "restart_unconfirmed=1\nvsync=0\n";
    }
    _putenv_s("TSUKI_ENGINE_FPS", "240");
    v.vsync = true;
    TsukiSettings::Load();
    assert(!v.vsync);
    _putenv_s("TSUKI_ENGINE_FPS", "");
    TsukiSettings::Save();
    const std::string engine = Read(path);
    for (const char* key : {"render_resolution", "engine_fps", "fps_limit", "readback", "restart_"}) {
        assert(engine.find(key) == std::string::npos);
    }
    // A fresh Values has the overlay's default items: FPS and frametime, not the old upscaler bit.
    const TsukiSettings::Values fresh;
    assert(fresh.overlay_items == 3u);
    {
        // Spec 7: the old TSUKI_* names are not aliases.
        std::ofstream out(path);
        out << "fsr1=0\n";
    }
    _putenv_s("GOW3_FSR1", "1"); // rename-check: ok
    TsukiSettings::Load();
    assert(!v.fsr1);
    _putenv_s("GOW3_FSR1", ""); // rename-check: ok
    _putenv_s("TSUKI_FSR1", "1");
    TsukiSettings::Load();
    assert(v.fsr1);
    _putenv_s("TSUKI_FSR1", "");
    std::remove(path);
    std::puts("PASS: graphics settings");
}
