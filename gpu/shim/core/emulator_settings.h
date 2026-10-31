// tsuki: shadPS4 settings used by the video core, read once from TSUKI_* environment
// variables (defaults match shadPS4 except the pipeline cache, which is on).
#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include "common/types.h"

u32 TsukiDisplayRefreshHz(); // tsukigpu.cpp: primary display refresh rate, 60 when unknown

enum GpuReadbacksMode : int { Disabled, Relaxed, Precise };

class EmulatorSettingsImpl {
public:
    static EmulatorSettingsImpl* GetInstance() { static EmulatorSettingsImpl s; return &s; }
    static bool Flag(const char* name, bool fallback) {
        const char* v = std::getenv(name);
        return v ? (v[0] == '1' || v[0] == 'y' || v[0] == 't') : fallback;
    }
    static long Number(const char* name, long fallback) {
        const char* v = std::getenv(name);
        return v ? std::strtol(v, nullptr, 10) : fallback;
    }
    s32 GetGpuId() { static const auto value = s32(Number("TSUKI_GPU_ID", -1)); return value; }
    u32 GetInternalScreenWidth() { static const auto value = u32(Number("TSUKI_INTERNAL_WIDTH", 1920)); return value; }
    u32 GetInternalScreenHeight() { static const auto value = u32(Number("TSUKI_INTERNAL_HEIGHT", 1080)); return value; }
    std::string GetPresentMode() { const char* v = std::getenv("TSUKI_PRESENT_MODE"); return v ? v : "Mailbox"; }
    // tsuki: Relaxed by default: without readbacks FaceGen reads stale GPU-written vertices
    // (vertex explosions); in the test scene it costs no measurable frame rate.
    u32 GetReadbacksMode() { static const auto value = u32(Number("TSUKI_READBACKS", GpuReadbacksMode::Relaxed)); return value; }
    // tsuki: TSUKI_VBLANK_HZ=0 (uncapped presets): vblank runs at 480 Hz so a finished frame is
    // shown within ~2 ms instead of waiting for the next display-rate vblank (below the display
    // rate frames alternated 10/20 ms at 100 Hz: judder), and GetFrameLimit() caps the rate at
    // the display refresh, at most 120 (above ~120 FPS movement timing breaks: running slows).
    // An explicit TSUKI_VBLANK_HZ is used as given, without a limit (measurements).
    u32 GetVblankFrequency() {
        static const u32 value = [] {
            const long hz = Number("TSUKI_VBLANK_HZ", 60);
            return hz > 0 ? u32(hz) : 480u;
        }();
        return value;
    }
    /// Frames per second the present thread lets through; 0 = no limit. TSUKI_FPS_LIMIT overrides.
    u32 GetFrameLimit() {
        static const u32 value = [] {
            const long limit = Number("TSUKI_FPS_LIMIT", -1);
            if (limit >= 0) return u32(limit);
            return Number("TSUKI_VBLANK_HZ", 60) > 0 ? 0u : std::min<u32>(TsukiDisplayRefreshHz(), 120);
        }();
        return value;
    }
    bool IsCopyGpuBuffers() { static const auto value = Flag("TSUKI_COPY_GPU_BUFFERS", false); return value; }
    bool IsDirectMemoryAccessEnabled() { static const auto value = Flag("TSUKI_DIRECT_MEMORY_ACCESS", false); return value; }
    bool IsDumpShaders() { static const auto value = Flag("TSUKI_DUMP_SHADERS", false); return value; }
    bool IsHdrAllowed() { static const auto value = Flag("TSUKI_HDR", false); return value; }
    bool IsNullGPU() { static const auto value = Flag("TSUKI_NULL_GPU", false); return value; }
    bool IsPatchShaders() { return false; }
    bool IsPipelineCacheArchived() { return false; }
    bool IsPipelineCacheEnabled() { static const auto value = Flag("TSUKI_PIPELINE_CACHE", true); return value; }
    bool IsReadbackLinearImagesEnabled() { static const auto value = Flag("TSUKI_READBACK_LINEAR", false); return value; }
    bool IsRenderdocEnabled() { return false; }
    bool IsShaderCollect() { return false; }
    bool IsUserfaultfdTracking() { return false; }
    bool IsVkCrashDiagnosticEnabled() { return false; }
    bool IsVkGuestMarkersEnabled() { static const auto value = Flag("TSUKI_VK_MARKERS", false); return value; }
    bool IsVkHostMarkersEnabled() { static const auto value = Flag("TSUKI_VK_MARKERS", false); return value; }
    bool IsVkValidationCoreEnabled() { return true; }
    bool IsVkValidationEnabled() { static const auto value = Flag("TSUKI_VK_VALIDATION", false); return value; }
    bool IsVkValidationGpuEnabled() { return false; }
    bool IsVkValidationSyncEnabled() { static const auto value = Flag("TSUKI_VK_VALIDATION_SYNC", false); return value; }
};
#define EmulatorSettings (*EmulatorSettingsImpl::GetInstance())
