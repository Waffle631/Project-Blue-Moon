// SPDX-License-Identifier: GPL-2.0-or-later
#include "tsuki_perf_stats.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include "video_core/renderer_vulkan/vk_instance.h"

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#else
#include <filesystem>
#include <fstream>
#include <sstream>
#include <time.h>
#include <unistd.h>
#endif

namespace TsukiPerfStats {

namespace {

std::mutex mutex;
Sample latest;
std::once_flag started;
constexpr double GiB = 1024.0 * 1024.0 * 1024.0;

// VK_EXT_memory_budget, the same on both systems.
void Vram(const Vulkan::Instance* instance, Sample& s) {
    if (instance->CanReportMemoryUsage()) {
        s.vram_used_gb = double(instance->GetDeviceMemoryUsage()) / GiB;
        s.vram_total_gb = double(instance->GetDeviceMemoryBudgetNow()) / GiB;
    }
}

#ifdef _WIN32
uint64_t Ticks(const FILETIME& time) {
    return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

// This process's use of the GPU's 3D engines, as Task Manager shows it ("GPU Engine" counters,
// any vendor). Negative until the second sample or without the counters.
class GpuUsage {
public:
    GpuUsage() {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
            query = nullptr;
            return;
        }
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) !=
            ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = nullptr;
            return;
        }
        PdhCollectQueryData(query);
        pid = L"pid_" + std::to_wstring(GetCurrentProcessId()) + L"_";
    }

    float Percent() {
        if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS) {
            return -1.0f;
        }
        DWORD size = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                         nullptr) != PDH_MORE_DATA) {
            return -1.0f;
        }
        buffer.resize(size);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                         items) != ERROR_SUCCESS) {
            return -1.0f;
        }
        double total = 0.0;
        for (DWORD i = 0; i < count; ++i) {
            const wchar_t* name = items[i].szName;
            if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA && wcsstr(name, pid.c_str()) &&
                wcsstr(name, L"engtype_3D")) {
                total += items[i].FmtValue.doubleValue;
            }
        }
        return std::clamp(float(total), 0.0f, 100.0f);
    }

private:
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    std::wstring pid;
    std::vector<unsigned char> buffer;
};

void Run(const Vulkan::Instance* instance) {
    GpuUsage gpu;
    FILETIME created, ended, kernel, user, now;
    uint64_t last_process = 0, last_wall = 0;
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    while (true) {
        Sample s;
        s.gpu_name = std::string(instance->GetModelName());
        GetSystemTimeAsFileTime(&now);
        if (GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) {
            const uint64_t process = Ticks(kernel) + Ticks(user), wall = Ticks(now);
            if (last_wall) {
                s.cpu_percent = CpuPercent(process - last_process, wall - last_wall, cores);
            }
            last_process = process;
            last_wall = wall;
        }
        PROCESS_MEMORY_COUNTERS memory{};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
            s.ram_used_gb = double(memory.WorkingSetSize) / GiB;
        }
        MEMORYSTATUSEX system{.dwLength = sizeof(MEMORYSTATUSEX)};
        if (GlobalMemoryStatusEx(&system)) {
            s.ram_total_gb = double(system.ullTotalPhys) / GiB;
        }
        Vram(instance, s);
        s.gpu_percent = gpu.Percent();
        {
            std::scoped_lock lock{mutex};
            latest = std::move(s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
#else
uint64_t Nanoseconds(clockid_t clock) {
    timespec time{};
    clock_gettime(clock, &time);
    return uint64_t(time.tv_sec) * 1000000000u + uint64_t(time.tv_nsec);
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// This process's DRM clients, from the fdinfo of each descriptor open on /dev/dri: the per-process
// counterpart of Windows' "GPU Engine" counters.
std::vector<DrmClient> DrmClients() {
    std::vector<DrmClient> clients;
    std::error_code error;
    for (const auto& fd : std::filesystem::directory_iterator("/proc/self/fd", error)) {
        const auto target = std::filesystem::read_symlink(fd.path(), error);
        if (error || target.native().rfind("/dev/dri/", 0) != 0) {
            continue;
        }
        if (auto client = ParseDrmFdinfo(ReadText("/proc/self/fdinfo" / fd.path().filename()))) {
            clients.push_back(std::move(*client));
        }
    }
    return clients;
}

// No test for this loop or the /proc reads: they depend on the machine (the parsers are tested in
// tests/test_perf_overlay.cpp).
void Run(const Vulkan::Instance* instance) {
    uint64_t last_process = 0, last_wall = 0;
    std::optional<uint64_t> last_gfx;
    float gpu_percent = -1.0f;
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const double page = double(sysconf(_SC_PAGESIZE));
    while (true) {
        Sample s;
        s.gpu_name = std::string(instance->GetModelName());
        // 100 % is every core, as on Windows.
        const uint64_t process = Nanoseconds(CLOCK_PROCESS_CPUTIME_ID), wall = Nanoseconds(CLOCK_MONOTONIC);
        if (last_wall) {
            s.cpu_percent = CpuPercent(process - last_process, wall - last_wall, cores);
        }
        // Resident set, like WorkingSetSize: it counts the touched pages of the direct memory pool.
        if (const auto pages = ParseStatmResidentPages(ReadText("/proc/self/statm"))) {
            s.ram_used_gb = double(*pages) * page / GiB;
        }
        if (const long total = sysconf(_SC_PHYS_PAGES); total > 0) {
            s.ram_total_gb = double(total) * page / GiB;
        }
        Vram(instance, s);
        // Only the graphics engine, as Windows counts only engtype_3D. A client that closed takes its
        // time with it: when the sum goes back, the last figure stays until the next sample.
        const auto gfx = SumGfxNs(DrmClients());
        if (!gfx) {
            gpu_percent = -1.0f;
        } else if (last_gfx && last_wall && *gfx >= *last_gfx) {
            gpu_percent = CpuPercent(*gfx - *last_gfx, wall - last_wall, 1);
        }
        s.gpu_percent = gpu_percent;
        last_gfx = gfx;
        last_process = process;
        last_wall = wall;
        {
            std::scoped_lock lock{mutex};
            latest = std::move(s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
#endif

} // namespace

void Start(const Vulkan::Instance& instance) {
    // ponytail: never joined; a thread that ended while the game ran crashed in ntdll
    // (vk_pipeline_serialization.cpp, StopWarmUp). It sleeps between samples.
    std::call_once(started, [&] { std::thread(Run, &instance).detach(); });
}

Sample Latest() {
    std::scoped_lock lock{mutex};
    return latest;
}

} // namespace TsukiPerfStats
