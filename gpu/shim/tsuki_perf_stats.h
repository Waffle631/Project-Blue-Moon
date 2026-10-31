// SPDX-License-Identifier: GPL-2.0-or-later
// tsuki: numbers for the performance overlay (tsuki_overlay.cpp). A thread started once samples
// CPU, RAM, GPU use (Windows counters; on Linux /proc and the DRM fdinfo) and VRAM
// (VK_EXT_memory_budget) twice a second; a value that cannot be measured stays negative and shows as "n/a".

#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Vulkan {
class Instance;
}

namespace TsukiPerfStats {

/// Overlay items (TsukiSettings::Values::overlay_items). Bit 2 was the temporal upscaler's name,
/// removed with the upscaler's options (Spec 7 §10); it stays free so the other bits keep their meaning.
enum Item : unsigned {
    Fps = 1u << 0,
    FrameTime = 1u << 1,
    Gpu = 1u << 3,
    Cpu = 1u << 4,
    Ram = 1u << 5,
    Vram = 1u << 6,
    AllItems = Fps | FrameTime | Gpu | Cpu | Ram | Vram,
};
inline constexpr unsigned DefaultItems = Fps | FrameTime;

struct Sample {
    float fps = 0.0f, frame_ms = 0.0f;
    std::string gpu_name;
    float gpu_percent = -1.0f, cpu_percent = -1.0f;
    double ram_used_gb = -1.0, ram_total_gb = -1.0, vram_used_gb = -1.0, vram_total_gb = -1.0;
};

/// Process CPU time over wall time on every core, 0 to 100; -1 without a wall interval.
inline float CpuPercent(uint64_t process_delta, uint64_t wall_delta, unsigned cores) {
    if (!wall_delta || !cores) {
        return -1.0f;
    }
    return std::clamp(float(double(process_delta) * 100.0 / (double(wall_delta) * cores)), 0.0f, 100.0f);
}

/// The leading number of `text` after any spaces or tabs.
inline std::optional<uint64_t> LeadingNumber(std::string_view text) {
    const size_t start = text.find_first_not_of(" \t");
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data() + start, text.data() + text.size(), value);
    if (error != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

/// Linux: the resident pages of /proc/self/statm, its second column.
inline std::optional<uint64_t> ParseStatmResidentPages(std::string_view statm) {
    const size_t space = statm.find(' ');
    if (space == std::string_view::npos) {
        return std::nullopt;
    }
    return LeadingNumber(statm.substr(space));
}

/// Linux: one DRM client of this process, from /proc/self/fdinfo/<fd> of a /dev/dri descriptor
/// (the kernel's drm-usage-stats keys; amdgpu writes drm-engine-gfx only once the client used it).
struct DrmClient {
    std::string pdev;
    uint64_t client_id = 0, gfx_ns = 0, vram_resident_kib = 0;
};

/// nullopt without drm-client-id or drm-engine-gfx: another driver, an older kernel, or a client that
/// never drew (RADV also opens the integrated GPU).
inline std::optional<DrmClient> ParseDrmFdinfo(std::string_view fdinfo) {
    DrmClient client;
    bool id = false, gfx = false;
    while (!fdinfo.empty()) {
        const size_t newline = fdinfo.find('\n');
        const std::string_view line = fdinfo.substr(0, newline);
        fdinfo = newline == std::string_view::npos ? std::string_view{} : fdinfo.substr(newline + 1);
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string_view key = line.substr(0, colon), value = line.substr(colon + 1);
        if (key == "drm-pdev") {
            const size_t start = value.find_first_not_of(" \t");
            client.pdev = start == std::string_view::npos ? "" : std::string(value.substr(start));
        } else if (key == "drm-client-id" || key == "drm-engine-gfx" || key == "drm-resident-vram") {
            const auto number = LeadingNumber(value);
            if (!number) {
                continue;
            }
            if (key == "drm-client-id") {
                client.client_id = *number;
                id = true;
            } else if (key == "drm-engine-gfx") {
                client.gfx_ns = *number;
                gfx = true;
            } else {
                client.vram_resident_kib = *number;
            }
        }
    }
    if (!id || !gfx) {
        return std::nullopt;
    }
    return client;
}

/// Graphics-engine time of the clients, each counted once: a dup'd descriptor shows the same client.
/// nullopt without clients, so the overlay shows "n/a".
inline std::optional<uint64_t> SumGfxNs(const std::vector<DrmClient>& clients) {
    if (clients.empty()) {
        return std::nullopt;
    }
    uint64_t total = 0;
    for (size_t i = 0; i < clients.size(); ++i) {
        const auto same = [&](const DrmClient& c) {
            return c.client_id == clients[i].client_id && c.pdev == clients[i].pdev;
        };
        if (std::none_of(clients.begin(), clients.begin() + i, same)) {
            total += clients[i].gfx_ns;
        }
    }
    return total;
}

/// The overlay text for the items in `mask`: one line each, or one line when `row`.
inline std::string Format(const Sample& s, unsigned mask, bool row) {
    std::string out;
    char part[160];
    const auto add = [&] {
        if (!out.empty()) {
            out += row ? "   " : "\n";
        }
        out += part;
    };
    const auto pair = [](char* to, size_t size, const char* label, double used, double total) {
        if (used < 0.0) {
            std::snprintf(to, size, "%s n/a", label);
        } else if (total < 0.0) {
            std::snprintf(to, size, "%s %.1f GB", label, used);
        } else {
            std::snprintf(to, size, "%s %.1f / %.1f GB", label, used, total);
        }
    };
    if (mask & Fps) {
        std::snprintf(part, sizeof(part), "%.0f FPS", s.fps);
        add();
    }
    if (mask & FrameTime) {
        std::snprintf(part, sizeof(part), "%.1f ms", s.frame_ms);
        add();
    }
    if (mask & Gpu) {
        const char* name = s.gpu_name.empty() ? "GPU" : s.gpu_name.c_str();
        if (s.gpu_percent < 0.0f) {
            std::snprintf(part, sizeof(part), "%s n/a", name);
        } else {
            std::snprintf(part, sizeof(part), "%s %.0f%%", name, s.gpu_percent);
        }
        add();
    }
    if (mask & Cpu) {
        if (s.cpu_percent < 0.0f) {
            std::snprintf(part, sizeof(part), "CPU n/a");
        } else {
            std::snprintf(part, sizeof(part), "CPU %.0f%%", s.cpu_percent);
        }
        add();
    }
    if (mask & Ram) {
        pair(part, sizeof(part), "RAM", s.ram_used_gb, s.ram_total_gb);
        add();
    }
    if (mask & Vram) {
        pair(part, sizeof(part), "VRAM", s.vram_used_gb, s.vram_total_gb);
        add();
    }
    return out;
}

/// Starts the sampling thread the first time; later calls do nothing.
void Start(const Vulkan::Instance& instance);
/// The latest CPU, RAM, GPU and VRAM values (the caller fills FPS and frametime).
Sample Latest();

} // namespace TsukiPerfStats
