// Performance overlay text and settings (tsuki_perf_stats.h, tsuki_settings.cpp).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "tsuki_perf_stats.h"
#include "tsuki_settings.h"
#ifndef _WIN32
#define _putenv_s(name, value) setenv(name, value, 1)
#endif

namespace P = TsukiPerfStats;

static std::string Read(const char* path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int main() {
    assert(P::CpuPercent(50, 100, 1) == 50.0f);
    assert(P::CpuPercent(400, 100, 8) == 50.0f);
    assert(P::CpuPercent(5000, 100, 4) == 100.0f);
    assert(P::CpuPercent(10, 0, 4) < 0.0f && P::CpuPercent(10, 100, 0) < 0.0f);

    P::Sample s;
    s.fps = 238.4f;
    s.frame_ms = 4.19f;
    s.gpu_name = "RTX 4070";
    s.gpu_percent = 87.2f;
    s.cpu_percent = 34.0f;
    s.ram_used_gb = 6.12;
    s.ram_total_gb = 31.9;
    s.vram_used_gb = 5.4;
    s.vram_total_gb = 12.0;
    // Spec 7 §10: the upscaler item is gone (bit 2 stays free, so a saved mask keeps its meaning).
    assert(P::Format(s, P::DefaultItems, true) == "238 FPS   4.2 ms");
    assert(P::Format(s, 1u << 2, true).empty() && !(P::AllItems & 1u << 2));
    assert(P::Format(s, P::AllItems, false) ==
           "238 FPS\n4.2 ms\nRTX 4070 87%\nCPU 34%\nRAM 6.1 / 31.9 GB\nVRAM 5.4 / 12.0 GB");
    assert(P::Format(s, P::Cpu | P::Vram, true) == "CPU 34%   VRAM 5.4 / 12.0 GB");
    assert(P::Format(s, 0, true).empty());

    P::Sample unknown;
    assert(P::Format(unknown, P::Gpu | P::Cpu | P::Ram | P::Vram, false) ==
           "GPU n/a\nCPU n/a\nRAM n/a\nVRAM n/a");
    unknown.ram_used_gb = 2.0;
    assert(P::Format(unknown, P::Ram, true) == "RAM 2.0 GB");

    // §9: the resident pages of /proc/self/statm are its second column.
    assert(P::ParseStatmResidentPages("123456 7890 321 1 0 4000 0\n") == 7890u);
    assert(!P::ParseStatmResidentPages("123456\n") && !P::ParseStatmResidentPages(""));

    // §9: an amdgpu fdinfo (kernel 7.2, keys trimmed); a dup of the same client counts once.
    const auto fdinfo = [](const char* id, const char* gfx) {
        return std::string("pos:\t0\nflags:\t02100002\ndrm-driver:\tamdgpu\ndrm-client-id:\t") + id +
               "\ndrm-pdev:\t0000:03:00.0\npasid:\t64\ndrm-memory-gtt: \t8208 KiB\n"
               "drm-resident-vram:\t69844 KiB\n" + gfx + "drm-engine-compute:\t544923 ns\n";
    };
    const auto seven = P::ParseDrmFdinfo(fdinfo("7", "drm-engine-gfx:\t1000000 ns\n"));
    const auto nine = P::ParseDrmFdinfo(fdinfo("9", "drm-engine-gfx:\t2000000 ns\n"));
    assert(seven && seven->pdev == "0000:03:00.0" && seven->client_id == 7 && seven->gfx_ns == 1000000 &&
           seven->vram_resident_kib == 69844);
    assert(nine && nine->client_id == 9);
    assert(P::SumGfxNs({*seven, *nine}) == 3000000u && P::SumGfxNs({*seven, *nine, *seven}) == 3000000u);
    assert(!P::SumGfxNs({}));

    // §9: no drm-engine-gfx (another driver, an old kernel, or a client that never drew) leaves no client,
    // so no sum: gpu_percent stays negative and shows as "GPU n/a" (the `unknown` sample above).
    assert(!P::ParseDrmFdinfo(fdinfo("7", "")) && !P::ParseDrmFdinfo("drm-engine-gfx:\t5 ns\n"));

    const char* path = "perf-overlay-test.ini";
    {
        std::ofstream out(path);
        out << "show_fps=1\noverlay_fps=0\noverlay_frametime=1\noverlay_upscaler=1\noverlay_gpu=1\n"
               "overlay_cpu=1\noverlay_ram=0\noverlay_vram=1\noverlay_scale=400\noverlay_corner=2\n"
               "overlay_opacity=-5\noverlay_layout=1\nmenu_tab=2\n";
    }
    _putenv_s("TSUKI_CONFIG", path);
    auto& v = TsukiSettings::Get();
    assert(v.overlay_items == P::DefaultItems && v.overlay_scale == 100 && v.overlay_corner == 1);
    TsukiSettings::Load();
    assert(v.overlay_items == (P::FrameTime | P::Gpu | P::Cpu | P::Vram));  // overlay_upscaler=1 is ignored
    assert(v.overlay_scale == 300 && v.overlay_corner == 2 && v.overlay_opacity == 0);
    assert(v.overlay_layout == 1 && v.menu_tab == 2);  // the last of the three tabs
    v.overlay_items = P::Fps | P::Ram;
    v.overlay_scale = 150;
    v.overlay_opacity = 70;
    v.menu_tab = 1;
    TsukiSettings::Save();
    const std::string saved = Read(path);
    for (const char* line : {"overlay_fps=1\n", "overlay_frametime=0\n", "overlay_ram=1\n", "overlay_vram=0\n",
                             "overlay_scale=150\n", "overlay_corner=2\n", "overlay_opacity=70\n",
                             "overlay_layout=1\n", "menu_tab=1\n"}) {
        assert(saved.find(line) != std::string::npos);
    }
    {
        std::ofstream out(path);
        out << "overlay_corner=9\nmenu_tab=-1\noverlay_scale=10\n";
    }
    TsukiSettings::Load();
    assert(v.overlay_corner == 1 && v.menu_tab == 0 && v.overlay_scale == 50);
    {
        std::ofstream out(path);
        out << "menu_tab=3\n";  // the removed fourth tab of an old tsuki.ini
    }
    v.menu_tab = 1;
    TsukiSettings::Load();
    assert(v.menu_tab == 0);
    std::remove(path);
    std::puts("PASS: performance overlay");
}
