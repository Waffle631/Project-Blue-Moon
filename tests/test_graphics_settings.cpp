// gow3.ini graphics keys (gow3_settings.cpp, gow3_graphics.h).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "gow3_graphics.h"
#include "gow3_settings.h"
#ifndef _WIN32
#define _putenv_s(name, value) setenv(name, value, 1) // "" counts as unset for Load()
#endif

namespace G = Gow3Graphics;

static std::string Read(const char* path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int main() {
    assert(G::ParseDisplayMode("borderless") == G::Borderless && G::ParseDisplayMode("bogus") == G::Windowed);
    assert(G::ParseFpsLimit(240) == 240 && G::ParseFpsLimit(75) == 0);
    assert(G::ParseResolution("1440p") == 3 && G::ParseResolution("8K") == 0);
    assert(G::ParseEngineFps(240) == 240 && G::ParseEngineFps(90) == 120);
    assert(G::ParseFramesQueued(2) == 2 && G::ParseFramesQueued(0) == 0 && G::ParseFramesQueued(5) == 1);
    assert(G::ParseRcasStrength(100) == 100 && G::ParseRcasStrength(101) == 75 && G::ParseRcasStrength(-1) == 75);
    assert(G::RcasAttenuation(100) == 0.0f && G::RcasAttenuation(0) == 2.0f && G::RcasAttenuation(75) == 0.5f);
    assert(G::FsrPassRuns(true, false, 1280, 720, 2560, 1440) && !G::FsrPassRuns(false, false, 1280, 720, 2560, 1440));
    assert(G::FsrPassRuns(false, true, 1920, 1080, 1920, 1080) && !G::FsrPassRuns(true, false, 1920, 1080, 1920, 1080));
    assert(!G::FsrPassRuns(true, true, 2560, 1440, 1920, 1080));  // larger image: regular scaling
    assert(G::EffectiveFps(0, 120) == 120 && G::EffectiveFps(240, 120) == 120 && G::EffectiveFps(60, 240) == 60);
    // An unpatched engine steps 1/60 s per frame: presenting faster speeds the game up.
    assert(G::PresentLimit(120, 60) == 60 && G::PresentLimit(240, 120) == 120 && G::PresentLimit(240, 240) == 240);
    assert(G::PresentLimit(0, 60) == 0 && G::PresentLimit(30, 60) == 30);  // 0: paced by vblank

    const char* path = "graphics-settings-test.ini";
    {
        std::ofstream out(path);
        out << "display_mode=borderless\nvsync=0\nfps_limit=240\nasync_shaders=0\n"
               "render_resolution=1440p\nengine_fps=240\ndeferred_readback=0\nstale_readback=1\n"
               "fsr1=1\nrcas=0\nrcas_strength=40\nframes_queued=2\n";
    }
    _putenv_s("GOW3_CONFIG", path);
    _putenv_s("GOW3_ASYNC_SHADERS", "");
    auto& v = Gow3Settings::Get();
    Gow3Settings::Load();
    assert(v.display_mode == G::Borderless && !v.vsync && v.fps_limit == 240 && !v.async_shaders);
    assert(v.render_resolution == 3 && v.engine_fps == 240 && !v.deferred_readback && v.stale_readback);
    assert(v.fsr1 && !v.rcas && v.rcas_strength == 40 && v.frames_queued == 2);
    assert(!Gow3Settings::GraphicsNeedRestart());
    assert(v.applied_engine_fps == 240);  // no GOW3_ENGINE_FPS: the configured rate
    _putenv_s("GOW3_ENGINE_FPS", "60");    // run.py: the frame rate patch did not apply
    Gow3Settings::Load();
    assert(v.engine_fps == 240 && v.applied_engine_fps == 60 && !Gow3Settings::GraphicsNeedRestart());
    _putenv_s("GOW3_ENGINE_FPS", "");
    v.engine_fps = 120;
    assert(Gow3Settings::GraphicsNeedRestart());
    v.fps_limit = 60;  // live: no restart because of it
    v.engine_fps = 240;
    assert(!Gow3Settings::GraphicsNeedRestart());

    Gow3Settings::Save();
    const std::string saved = Read(path);
    for (const char* line : {"display_mode=borderless\n", "vsync=0\n", "fps_limit=60\n", "async_shaders=0\n",
                             "render_resolution=1440p\n", "engine_fps=240\n", "deferred_readback=0\n",
                             "stale_readback=1\n", "fsr1=1\n", "rcas=0\n", "rcas_strength=40\n",
                             "frames_queued=2\n"}) {
        assert(saved.find(line) != std::string::npos);
    }

    {
        std::ofstream out(path);
        out << "display_mode=huge\nfps_limit=7\nrender_resolution=8K\nengine_fps=1\n"
               "rcas_strength=250\nframes_queued=9\n";
    }
    _putenv_s("GOW3_ASYNC_SHADERS", "1");  // the environment still wins at start
    v.async_shaders = false;
    Gow3Settings::Load();
    assert(v.display_mode == G::Windowed && v.fps_limit == 0 && v.render_resolution == 0 && v.engine_fps == 120);
    assert(v.rcas_strength == 75 && v.frames_queued == 1);
    assert(v.async_shaders);
    {
        // An old gow3.ini with the removed God of War III keys: ignored, and not written back.
        std::ofstream out(path);
        out << "cheat_health=1\nred_orb_multiplier=5.0\ndamage_taken=0.5\nenemy_health_bar=1\nvsync=0\n";
    }
    v.vsync = true;
    Gow3Settings::Load();
    assert(!v.vsync);
    Gow3Settings::Save();
    const std::string old = Read(path);
    for (const char* key : {"cheat_", "orb_multiplier", "damage_", "enemy_health_bar"}) {
        assert(old.find(key) == std::string::npos);
    }
    {
        // Spec 7 §10: neither gow3.ini nor the environment can switch the temporal upscaler on, and
        // Save() does not write its keys (its code stays, with no inputs since Spec 1b).
        std::ofstream out(path);
        out << "upscaler=fsr3\npreset=2\njitter=0\nreactive=1\nobject_motion=0\nreactive_scale=4\n"
               "reactive_threshold=0.5\nreactive_max=0.5\ndebug_view=2\noutput_res=2560x1440\nsharpen=0\n"
               "sharpness=1.5\nfsr4_auto_exposure=0\nfsr4_invert_jitter=1\nlive_resolution=1\n"
               "overlay_upscaler=1\nfsr1=1\n";
    }
    for (const char* env : {"GOW3_UPSCALER=fsr3", "GOW3_UPSCALE_PRESET=3", "GOW3_JITTER=0", "GOW3_REACTIVE=1",
                            "GOW3_OBJECT_MOTION=0", "GOW3_FSR_SHARPNESS=1.5", "GOW3_REACTIVE_SCALE=4"}) {
        const std::string pair = env;
        _putenv_s(pair.substr(0, pair.find('=')).c_str(), pair.substr(pair.find('=') + 1).c_str());
    }
    v.fsr1 = false;
    Gow3Settings::Load();
    assert(v.upscaler == Gow3Settings::UpscalerOff && v.startup_upscaler == Gow3Settings::UpscalerOff);
    assert(v.preset == Gow3Settings::NativeAA && v.jitter && !v.reactive && v.object_motion);
    assert(v.reactive_scale == 1.0f && v.debug_view == Gow3Settings::DebugNone);
    assert(v.output_res == Gow3Settings::OutputDefault && v.sharpen && v.sharpness == 0.3f);
    assert(v.fsr4_auto_exposure && !v.fsr4_invert_jitter && !(v.overlay_items & 1u << 2));
    assert(v.fsr1);  // FSR 1 is spatial and stays
    Gow3Settings::Save();
    const std::string temporal = Read(path);
    for (const char* key : {"upscaler", "preset=", "jitter", "reactive", "object_motion", "debug_view", "output_res",
                            "sharpen", "sharpness", "fsr4_", "live_resolution"}) {
        assert(temporal.find(key) == std::string::npos);
    }
    assert(temporal.find("fsr1=1\n") != std::string::npos);
    // A fresh Values has the overlay's default items: FPS and frametime, not the old upscaler bit.
    const Gow3Settings::Values fresh;
    assert(fresh.overlay_items == 3u && fresh.upscaler == Gow3Settings::UpscalerOff);
    std::remove(path);
    std::puts("PASS: graphics settings");
}
