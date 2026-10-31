// SPDX-License-Identifier: GPL-2.0-or-later
// Spec 5 (AvPlayer): the vendored AvPlayer driven the way Tsukihime drives it (docs/avplayer.md §1).
// sceAvPlayerInit gets host allocators, a FileReplacement over a host file and an event callback that
// starts playback from StateReady on the controller thread. The test thread then polls IsActive and
// GetVideoData like the game's task_game, and an "av audio" thread polls GetAudioData. Stop comes in
// the same poll in which IsActive() turns false. That last step is what hung sceAvPlayerStop in the
// game (spec §2, docs/avplayer.md §3).
//
// Each case runs in a forked child whose fd 2 goes to a temporary file. A child whose step does not
// change for 15 s, or that outlives its deadline, is killed and the case fails; the report names the
// step it was in and its threads. A "join failed" line from Kernel::Thread
// (gpu/shim/core/libraries/kernel/threads.h) also fails it, and so does a complaint of FFmpeg's H.264
// decoder ("[h264 @"): the fixtures are valid, so it was fed bytes that are not the stream.
//
// Fixtures (tests/data, FFmpeg 8.1). They are H.264 Main at 29.97 fps with the moov first, like
// image/movie/, and without B-frames but for tiny_b150. tiny_v150 replicates
// phasetitle_01_00_en.mp4 (video only, 150 frames, 5.005 s); tiny_b150 is the same with up to two
// B-frames in a row, like 75 of the 134 videos (has_b_frames 1 or 2).
//   V="-c:v libx264 -profile:v main -bf 0 -pix_fmt yuv420p -crf 42 -preset veryslow -map_metadata -1
//      -fflags +bitexact -flags:v +bitexact -movflags +faststart"
//   ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30000/1001 -frames:v 150 $V -an tiny_v150.mp4
//   ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30000/1001 -frames:v 150 $V -bf 2 -an tiny_b150.mp4
//   ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30000/1001 -frames:v 20 $V -an tiny_v20.mp4
//   ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30000/1001 -f lavfi -i
//      sine=frequency=440:sample_rate=48000 -t 2 $V -c:a aac -ac 2 -ar 48000 -b:a 48k
//      -flags:a +bitexact tiny_av.mp4
//
// Usage: avplayer-test <data dir> [case ...]   (ninja -C out/gpu avplayer-test)
//        avplayer-test --movie-dir <dir> [--seconds N] [file.mp4 ...]   plays each video of <dir>
//        (Tsukihime/CUSA47999/image/movie) for N s, 3 by default, each in its own child (Sweep).
// AVPLAYER_TEST_GDB=1 adds gdb's stacks of a hung child to the report; AVPLAYER_TEST_KEEP=<dir> keeps
// each case's stderr in <dir>/<case>.stderr (run with TSUKI_TRACE_AVPLAYER=1 to see every call).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/libraries/avplayer/avplayer.h"

namespace Libraries::AvPlayer {
// Exported by libtsukigpu.so (avplayer.cpp); avplayer.h only declares RegisterLib.
s32 PS4_SYSV_ABI sceAvPlayerAddSource(AvPlayerHandle handle, const char* filename);
s32 PS4_SYSV_ABI sceAvPlayerClose(AvPlayerHandle handle);
u64 PS4_SYSV_ABI sceAvPlayerCurrentTime(AvPlayerHandle handle);
s32 PS4_SYSV_ABI sceAvPlayerEnableStream(AvPlayerHandle handle, u32 stream_id);
bool PS4_SYSV_ABI sceAvPlayerGetAudioData(AvPlayerHandle handle, AvPlayerFrameInfo* p_info);
s32 PS4_SYSV_ABI sceAvPlayerGetStreamInfo(AvPlayerHandle handle, u32 stream_id,
                                          AvPlayerStreamInfo* p_info);
bool PS4_SYSV_ABI sceAvPlayerGetVideoData(AvPlayerHandle handle, AvPlayerFrameInfo* video_info);
AvPlayerHandle PS4_SYSV_ABI sceAvPlayerInit(AvPlayerInitData* data);
bool PS4_SYSV_ABI sceAvPlayerIsActive(AvPlayerHandle handle);
s32 PS4_SYSV_ABI sceAvPlayerJumpToTime(AvPlayerHandle handle, uint64_t time);
s32 PS4_SYSV_ABI sceAvPlayerPostInit(AvPlayerHandle handle, AvPlayerPostInitData* data);
s32 PS4_SYSV_ABI sceAvPlayerSetAvSyncMode(AvPlayerHandle handle, AvPlayerAvSyncMode sync_mode);
s32 PS4_SYSV_ABI sceAvPlayerSetLooping(AvPlayerHandle handle, bool loop_flag);
s32 PS4_SYSV_ABI sceAvPlayerStart(AvPlayerHandle handle);
s32 PS4_SYSV_ABI sceAvPlayerStop(AvPlayerHandle handle);
s32 PS4_SYSV_ABI sceAvPlayerStreamCount(AvPlayerHandle handle);
} // namespace Libraries::AvPlayer

using namespace Libraries::AvPlayer;
using Clock = std::chrono::steady_clock;

namespace {

constexpr s32 kOperationFailed = s32(0x806A0002); // what the game's second Stop gets (docs §1)
constexpr s32 kLoopingBack = s32(0x806A00A1);     // WarningId of a looping clip at its end
constexpr s32 kJumpComplete = s32(0x806A00A3);    // WarningId of a finished JumpToTime
constexpr double kFrameMs = 1001.0 / 30;          // the fixtures run at 29.97 fps

std::string g_data;

// Shared with the parent: the step the child is in, and how many steps it has taken. The sweep's
// child also leaves its counts and the message of a failed check, which its parent's line reports.
struct Progress {
    std::atomic<unsigned> steps;
    char step[256];
    char failure[256];
    std::atomic<int> frames, blocks;
    std::atomic<long long> end_ms, late_ms;
};
Progress* g_progress = nullptr;
constexpr long long kStallMs = 15000;
bool g_sweep = false; // --movie-dir: one line per video, so a failed check is not printed apart

long long MsSince(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void SleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string Data(const char* name) {
    return g_data + "/" + name;
}

[[gnu::format(printf, 1, 2)]] void Step(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(g_progress->step, sizeof(g_progress->step), fmt, args);
    va_end(args);
    ++g_progress->steps;
}

[[noreturn, gnu::format(printf, 1, 2)]] void Fail(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(g_progress->failure, sizeof(g_progress->failure), fmt, args);
    va_end(args);
    if (!g_sweep) std::printf("  failed: %s\n", g_progress->failure);
    std::fflush(stdout);
    std::_Exit(1);
}

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) Fail(__VA_ARGS__);                                                            \
    } while (0)

template <class F>
auto Timed(long long& ms, F&& f) {
    const auto begin = Clock::now();
    auto result = f();
    ms = MsSince(begin);
    return result;
}

int ThreadCount() {
    int count = 0;
    if (DIR* dir = opendir("/proc/self/task")) {
        while (const dirent* entry = readdir(dir)) count += entry->d_name[0] != '.';
        closedir(dir);
    }
    return count;
}

// ---- What the game hands to sceAvPlayerInit ----

std::atomic<long> g_live_allocations{0};

void* PS4_SYSV_ABI HostAllocate(void*, u32 alignment, u32 size) {
    const size_t align = std::max<size_t>(alignment, 16);
    void* memory = std::aligned_alloc(align, (size_t(size) + align - 1) / align * align);
    if (memory != nullptr) ++g_live_allocations;
    return memory;
}

void PS4_SYSV_ABI HostFree(void*, void* memory) {
    if (memory != nullptr) --g_live_allocations;
    std::free(memory);
}

// The game reads its videos through FileReplacement. With read_fail_from >= 0, every read that
// reaches that offset fails, which cuts the stream in the middle of the flow.
struct HostFile {
    int fd = -1;
    u64 size = 0;
    s64 read_fail_from = -1;
};

s32 PS4_SYSV_ABI FileOpen(void* object, const char* name) {
    auto* file = static_cast<HostFile*>(object);
    file->fd = ::open(name, O_RDONLY | O_CLOEXEC);
    if (file->fd < 0) return -1;
    struct stat st {};
    fstat(file->fd, &st);
    file->size = u64(st.st_size);
    return 0;
}

s32 PS4_SYSV_ABI FileClose(void* object) {
    auto* file = static_cast<HostFile*>(object);
    if (file->fd >= 0) ::close(file->fd);
    file->fd = -1;
    return 0;
}

s32 PS4_SYSV_ABI FileRead(void* object, u8* buffer, u64 position, u32 length) {
    auto* file = static_cast<HostFile*>(object);
    if (file->read_fail_from >= 0 && position + length > u64(file->read_fail_from)) return -1;
    const ssize_t read = pread(file->fd, buffer, length, off_t(position));
    return read < 0 ? -1 : s32(read);
}

u64 PS4_SYSV_ABI FileSize(void* object) {
    return static_cast<HostFile*>(object)->size;
}

struct Options {
    AvPlayerAvSyncMode sync = AvPlayerAvSyncMode::Default;
    s64 read_fail_from = -1;
    bool start_on_ready = true;
    bool looping = false; // the game loops titlebg1.mp4 only
    std::function<void(AvPlayerEvents)> on_event; // on the controller thread, after the start
};

struct Event {
    AvPlayerEvents id;
    long long ms;    // since Open
    s32 warning = 0; // the code a WarningId carries
};

const char* EventName(AvPlayerEvents id) {
    switch (id) {
    case AvPlayerEvents::StateStop: return "Stop";
    case AvPlayerEvents::StateReady: return "Ready";
    case AvPlayerEvents::StatePlay: return "Play";
    case AvPlayerEvents::StatePause: return "Pause";
    case AvPlayerEvents::StateBuffering: return "Buffering";
    case AvPlayerEvents::WarningId: return "Warning";
    default: return "Other";
    }
}

// One player, as one Init/Close pair of the game. The event callback points at it.
struct Session {
    explicit Session(Options options_ = {}) : options(std::move(options_)) {}
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    int Count(AvPlayerEvents id) {
        std::lock_guard lock(mutex);
        return int(std::count_if(events.begin(), events.end(),
                                 [id](const Event& e) { return e.id == id; }));
    }

    bool WaitFor(int ms, const std::function<bool()>& ready) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(ms), ready);
    }

    bool WaitStarted(int ms) {
        return WaitFor(ms, [this] { return started; });
    }

    bool WaitEvent(AvPlayerEvents id, int ms) {
        return WaitFor(ms, [this, id] {
            return std::any_of(events.begin(), events.end(),
                               [id](const Event& e) { return e.id == id; });
        });
    }

    int CountWarning(s32 code) {
        std::lock_guard lock(mutex);
        return int(std::count_if(events.begin(), events.end(), [code](const Event& e) {
            return e.id == AvPlayerEvents::WarningId && e.warning == code;
        }));
    }

    bool WaitWarning(s32 code, int ms) {
        return WaitFor(ms, [this, code] {
            return std::any_of(events.begin(), events.end(), [code](const Event& e) {
                return e.id == AvPlayerEvents::WarningId && e.warning == code;
            });
        });
    }

    Clock::time_point StartTime() {
        std::lock_guard lock(mutex);
        return start_time;
    }

    std::string EventList() {
        std::lock_guard lock(mutex);
        std::string list;
        for (const auto& event : events) {
            char code[16] = "";
            if (event.id == AvPlayerEvents::WarningId) {
                std::snprintf(code, sizeof(code), ":%#x", unsigned(event.warning));
            }
            list += (list.empty() ? "" : " ") + std::string(EventName(event.id)) + code + "@" +
                    std::to_string(event.ms);
        }
        return list.empty() ? "none" : list;
    }

    Options options;
    HostFile file;
    AvPlayerHandle handle = nullptr;
    Clock::time_point opened;
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<Event> events;
    bool started = false; // Start returned, called from the StateReady callback
    s32 start_result = 0;
    Clock::time_point start_time;
    bool has_audio = false;
    u64 duration_ms = 0;       // of the longest stream
    u64 video_duration_ms = 0; // of the video stream
};

void PS4_SYSV_ABI OnEvent(void* object, AvPlayerEvents id, s32, void* data) {
    auto* s = static_cast<Session*>(object);
    {
        std::lock_guard lock(s->mutex);
        const bool warning = id == AvPlayerEvents::WarningId && data != nullptr;
        s->events.push_back({id, MsSince(s->opened), warning ? *static_cast<const s32*>(data) : 0});
    }
    s->cv.notify_all();
    if (id == AvPlayerEvents::StateReady && s->options.start_on_ready) {
        // What the game does in this callback, on the controller thread: every stream is enabled,
        // then Start and SetLooping (docs/avplayer.md §1).
        const s32 count = sceAvPlayerStreamCount(s->handle);
        bool has_audio = false;
        u64 duration = 0, video_duration = 0;
        for (s32 index = 0; index < count; ++index) {
            AvPlayerStreamInfo info{};
            if (sceAvPlayerGetStreamInfo(s->handle, u32(index), &info) != 0) continue;
            has_audio |= info.type == AvPlayerStreamType::Audio;
            duration = std::max(duration, info.duration);
            if (info.type == AvPlayerStreamType::Video) video_duration = info.duration;
            sceAvPlayerEnableStream(s->handle, u32(index));
        }
        const auto start_time = Clock::now();
        const s32 result = sceAvPlayerStart(s->handle);
        sceAvPlayerSetLooping(s->handle, s->options.looping);
        {
            std::lock_guard lock(s->mutex);
            s->has_audio = has_audio;
            s->duration_ms = duration;
            s->video_duration_ms = video_duration;
            s->start_time = start_time;
            s->start_result = result;
            s->started = true;
        }
        s->cv.notify_all();
    }
    if (s->options.on_event) s->options.on_event(id);
}

// Init, PostInit and AddSource with the values the game passes (docs/avplayer.md §1).
s32 Open(Session& s, const std::string& path) {
    s.file.read_fail_from = s.options.read_fail_from;
    AvPlayerInitData init{};
    init.memory_replacement = {nullptr, HostAllocate, HostFree, HostAllocate, HostFree};
    init.file_replacement = {&s.file, FileOpen, FileClose, FileRead, FileSize};
    init.event_replacement = {&s, OnEvent};
    init.debug_level = AvPlayerDebuglevels::None;
    init.base_priority = 700;
    init.num_output_video_framebuffers = 3;
    init.auto_start = false;
    s.opened = Clock::now();
    s.handle = sceAvPlayerInit(&init);
    CHECK(s.handle != nullptr, "sceAvPlayerInit returned null");
    AvPlayerPostInitData post{};
    post.demux_video_buffer_size = 3 << 20;
    post.video_decoder_init.decoder_type.video_type = AvPlayerVideoDecoderType::Software2;
    post.audio_decoder_init.decoder_type.audio_type = AvPlayerAudioDecoderType::Default;
    sceAvPlayerPostInit(s.handle, &post);
    if (s.options.sync != AvPlayerAvSyncMode::Default) {
        sceAvPlayerSetAvSyncMode(s.handle, s.options.sync);
    }
    return sceAvPlayerAddSource(s.handle, path.c_str());
}

// The game's "av audio" thread: it polls IsActive and GetAudioData without pause and plays each
// block, which takes the block's duration (1024 samples at 48 kHz = 21.3 ms) on a real output.
class AudioConsumer {
public:
    explicit AudioConsumer(AvPlayerHandle handle) : m_thread([this, handle] { Run(handle); }) {}
    ~AudioConsumer() {
        Join();
    }
    void Join() {
        m_run = false;
        if (m_thread.joinable()) m_thread.join();
    }
    int Blocks() const {
        return m_blocks;
    }
    u64 LastTimestamp() const {
        return m_last_ts;
    }

private:
    void Run(AvPlayerHandle handle) {
        auto due = Clock::now();
        while (m_run) {
            AvPlayerFrameInfo info{};
            if (sceAvPlayerIsActive(handle) && Clock::now() >= due &&
                sceAvPlayerGetAudioData(handle, &info)) {
                const auto& audio = info.details.audio;
                const u64 frame_bytes = std::max<u64>(1, audio.channel_count) * 2;
                const u64 us =
                    audio.sample_rate ? audio.size / frame_bytes * 1'000'000 / audio.sample_rate : 0;
                due = std::max(due, Clock::now()) + std::chrono::microseconds(us);
                m_last_ts = info.timestamp;
                ++m_blocks;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }

    std::atomic<bool> m_run{true};
    std::atomic<int> m_blocks{0};
    std::atomic<u64> m_last_ts{0};
    std::thread m_thread; // last, so the counters exist when it starts
};

struct Video {
    int frames = 0;
    u64 last_ts = 0;
    long long max_early = LLONG_MIN; // ms a frame came before its timestamp, wall clock since Start
    long long max_late = LLONG_MIN;
    long long av_min = LLONG_MAX, av_max = LLONG_MIN, av_sum = 0; // video ts - last audio ts
    int av_count = 0;
    long long inactive_ms = -1; // since Start, when IsActive() turned false
    int backwards = 0;          // frames with a timestamp below the one before
};

// The game's task_game loop: GetVideoData, IsActive, and nothing else until IsActive() is false.
// Returns whether it turned false within budget_ms.
bool Drain(Session& s, Video& video, int budget_ms, const AudioConsumer* audio = nullptr) {
    const auto begin = Clock::now();
    while (MsSince(begin) < budget_ms) {
        AvPlayerFrameInfo info{};
        if (sceAvPlayerGetVideoData(s.handle, &info)) {
            const long long early = (long long)info.timestamp - MsSince(s.StartTime());
            video.max_early = std::max(video.max_early, early);
            video.max_late = std::max(video.max_late, -early);
            video.backwards += video.frames > 0 && info.timestamp < video.last_ts;
            video.last_ts = info.timestamp;
            ++video.frames;
            if (audio != nullptr && audio->Blocks() > 0) {
                const long long offset = (long long)info.timestamp - (long long)audio->LastTimestamp();
                video.av_min = std::min(video.av_min, offset);
                video.av_max = std::max(video.av_max, offset);
                video.av_sum += offset;
                ++video.av_count;
            }
        }
        if (!sceAvPlayerIsActive(s.handle)) {
            video.inactive_ms = MsSince(s.StartTime());
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return false;
}

std::string Describe(const Video& video) {
    char text[256];
    if (video.frames == 0) return "0 frames";
    std::snprintf(text, sizeof(text), "%d frames, last ts %llu, early <= %lld ms, late <= %lld ms",
                  video.frames, (unsigned long long)video.last_ts, video.max_early, video.max_late);
    std::string result = text;
    if (video.av_count > 0) {
        std::snprintf(text, sizeof(text), ", video - audio ts %lld..%lld (mean %lld) ms",
                      video.av_min, video.av_max, video.av_sum / video.av_count);
        result += text;
    }
    return result;
}

// ---- Cases ----

void PlaysToEndVideoOnly(const char* fixture) {
    Session s;
    Step("AddSource %s", fixture);
    CHECK(Open(s, Data(fixture)) == 0, "AddSource failed");
    CHECK(s.WaitStarted(2000) && s.start_result == 0, "no Start within 2 s (events %s)",
          s.EventList().c_str());
    Video video;
    Step("playing");
    const bool ended = Drain(s, video, 10000);
    // The end must reach the game as an event by itself (spec §4); the game's own Stop comes later.
    Step("waiting for StateStop before Stop");
    const bool stop_event = s.WaitEvent(AvPlayerEvents::StateStop, 1000);
    long long stop_ms = 0, close_ms = 0;
    Step("Stop");
    const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
    Step("Close");
    const s32 closed = Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
    std::printf("  %s; inactive at %lld ms; duration %llu ms; events %s\n", Describe(video).c_str(),
                video.inactive_ms, (unsigned long long)s.duration_ms, s.EventList().c_str());
    std::printf("  Stop %#x in %lld ms, Close %#x in %lld ms\n", stop, stop_ms, closed, close_ms);
    CHECK(ended, "IsActive() still true after 10 s");
    CHECK(video.frames == 150, "%d of 150 frames", video.frames);
    CHECK(video.max_early <= 150, "a frame came %lld ms before its timestamp", video.max_early);
    CHECK(stop_event, "no StateStop within 1 s of the end, before Stop");
    CHECK(stop_ms < 2000 && close_ms < 2000, "Stop %lld ms, Close %lld ms", stop_ms, close_ms);
}

void PlaysToEndVideoOnly() {
    PlaysToEndVideoOnly("tiny_v150.mp4");
}

// 75 of the game's 134 videos have B-frames (tools/probe_movies.sh), up to two in a row. The
// decoder holds back as many frames as it reorders, and the last ones still have to come out.
void PlaysToEndWithBFrames() {
    PlaysToEndVideoOnly("tiny_b150.mp4");
}

void PlaysToEndWithAudio() {
    Session s;
    Step("AddSource tiny_av");
    CHECK(Open(s, Data("tiny_av.mp4")) == 0, "AddSource failed");
    CHECK(s.WaitStarted(2000) && s.start_result == 0, "no Start within 2 s (events %s)",
          s.EventList().c_str());
    CHECK(s.has_audio, "GetStreamInfo shows no audio stream");
    AudioConsumer audio(s.handle);
    Video video;
    Step("playing");
    const bool ended = Drain(s, video, 10000, &audio);
    Step("waiting for StateStop before Stop");
    const bool stop_event = s.WaitEvent(AvPlayerEvents::StateStop, 1000);
    long long stop_ms = 0, close_ms = 0;
    Step("Stop");
    const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
    audio.Join();
    Step("Close");
    const s32 closed = Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
    std::printf("  %s; %d audio blocks, last ts %llu; inactive at %lld ms; duration %llu ms\n",
                Describe(video).c_str(), audio.Blocks(), (unsigned long long)audio.LastTimestamp(),
                video.inactive_ms, (unsigned long long)s.duration_ms);
    std::printf("  events %s; Stop %#x in %lld ms, Close %#x in %lld ms\n", s.EventList().c_str(),
                stop, stop_ms, closed, close_ms);
    CHECK(ended, "IsActive() still true after 10 s");
    CHECK(video.frames == 60, "%d of 60 frames", video.frames);
    CHECK(audio.Blocks() >= 1, "no audio block");
    CHECK(stop_event, "no StateStop within 1 s of the end, before Stop");
    CHECK(stop_ms < 2000 && close_ms < 2000, "Stop %lld ms, Close %lld ms", stop_ms, close_ms);
}

// The trigger seen in the game: a video-only clip ends, IsActive() turns false and Stop follows in
// the same poll. The first cycles keep the default clock, like the game; the rest deliver frames
// as soon as they are decoded (AvSyncMode None) to try the race many more times.
void EofThenStopTrigger() {
    constexpr int kCycles = 500, kGameClockCycles = 25;
    int min_frames = INT_MAX;
    long long max_cycle = 0, max_stop = 0, max_close = 0;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        Options options;
        if (cycle >= kGameClockCycles) options.sync = AvPlayerAvSyncMode::None;
        Session s(options);
        const auto begin = Clock::now();
        Step("cycle %d: AddSource", cycle);
        CHECK(Open(s, Data("tiny_v20.mp4")) == 0, "cycle %d: AddSource failed", cycle);
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "cycle %d: no Start within 2 s", cycle);
        Video video;
        Step("cycle %d: playing", cycle);
        const bool ended = Drain(s, video, 2000);
        long long stop_ms = 0, close_ms = 0;
        Step("cycle %d: Stop", cycle);
        Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
        Step("cycle %d: Close", cycle);
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        const long long cycle_ms = MsSince(begin);
        CHECK(ended, "cycle %d: IsActive() still true after 2 s (%s)", cycle,
              Describe(video).c_str());
        CHECK(cycle_ms < 2000, "cycle %d took %lld ms (Stop %lld, Close %lld)", cycle, cycle_ms,
              stop_ms, close_ms);
        min_frames = std::min(min_frames, video.frames);
        max_cycle = std::max(max_cycle, cycle_ms);
        max_stop = std::max(max_stop, stop_ms);
        max_close = std::max(max_close, close_ms);
        if ((cycle + 1) % 100 == 0) std::printf("  %d cycles\n", cycle + 1);
    }
    std::printf("  frames per cycle >= %d of 20; cycle <= %lld ms, Stop <= %lld ms, Close <= %lld "
                "ms\n",
                min_frames, max_cycle, max_stop, max_close);
}

// Nobody drains: the demuxer reaches EOF with frames still queued and waits on the decoder, which
// waits for a free buffer; then Stop.
void EofWithUndrainedBuffersThenStop() {
    for (int cycle = 0; cycle < 20; ++cycle) {
        Session s;
        Step("cycle %d: AddSource", cycle);
        CHECK(Open(s, Data("tiny_v20.mp4")) == 0, "cycle %d: AddSource failed", cycle);
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "cycle %d: no Start within 2 s", cycle);
        Step("cycle %d: waiting for EOF without draining", cycle);
        SleepMs(500);
        long long stop_ms = 0, close_ms = 0;
        Step("cycle %d: Stop", cycle);
        const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
        Step("cycle %d: Close", cycle);
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        CHECK(stop == 0, "cycle %d: Stop returned %#x", cycle, stop);
        CHECK(stop_ms < 2000 && close_ms < 2000, "cycle %d: Stop %lld ms, Close %lld ms", cycle,
              stop_ms, close_ms);
    }
}

void StopDuringPlaybackWithoutDraining() {
    for (int cycle = 0; cycle < 10; ++cycle) {
        Session s;
        Step("cycle %d: AddSource", cycle);
        CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "cycle %d: AddSource failed", cycle);
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "cycle %d: no Start within 2 s", cycle);
        SleepMs(500);
        long long stop_ms = 0, close_ms = 0;
        Step("cycle %d: Stop", cycle);
        const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
        Step("cycle %d: Close", cycle);
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        CHECK(stop == 0, "cycle %d: Stop returned %#x", cycle, stop);
        CHECK(stop_ms < 2000 && close_ms < 2000, "cycle %d: Stop %lld ms, Close %lld ms", cycle,
              stop_ms, close_ms);
    }
}

// Stop as soon as Start returns, then the game's second Stop, which must report the stop it did not
// make (the game gets 0x806a0002 there, docs/avplayer.md §1).
void StopRightAfterStartAndTwice() {
    for (int cycle = 0; cycle < 20; ++cycle) {
        Session s;
        Step("cycle %d: AddSource", cycle);
        CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "cycle %d: AddSource failed", cycle);
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "cycle %d: no Start within 2 s", cycle);
        long long first_ms = 0, second_ms = 0, close_ms = 0;
        Step("cycle %d: Stop", cycle);
        const s32 first = Timed(first_ms, [&] { return sceAvPlayerStop(s.handle); });
        const bool active = sceAvPlayerIsActive(s.handle);
        Step("cycle %d: second Stop", cycle);
        const s32 second = Timed(second_ms, [&] { return sceAvPlayerStop(s.handle); });
        Step("cycle %d: Close", cycle);
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        CHECK(first == 0, "cycle %d: first Stop returned %#x", cycle, first);
        CHECK(!active, "cycle %d: IsActive() true after Stop", cycle);
        CHECK(second == kOperationFailed, "cycle %d: second Stop returned %#x", cycle, second);
        CHECK(first_ms < 2000 && second_ms < 2000 && close_ms < 2000,
              "cycle %d: Stop %lld ms, second Stop %lld ms, Close %lld ms", cycle, first_ms,
              second_ms, close_ms);
    }
}

// Start on a player that is already playing (AvPlayerState::Start holds the source mutex
// exclusively and calls Stop, which takes it shared).
void StartWhilePlaying() {
    Session s;
    Step("AddSource tiny_v150");
    CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "AddSource failed");
    CHECK(s.WaitStarted(2000) && s.start_result == 0, "no Start within 2 s");
    SleepMs(300);
    long long start_ms = 0, stop_ms = 0, close_ms = 0;
    Step("Start while playing");
    const s32 start = Timed(start_ms, [&] { return sceAvPlayerStart(s.handle); });
    Video video;
    Step("playing after the second Start");
    Drain(s, video, 500);
    Step("Stop");
    Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
    Step("Close");
    Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
    std::printf("  Start %#x in %lld ms; then %s\n", start, start_ms, Describe(video).c_str());
    CHECK(start_ms < 2000 && stop_ms < 2000 && close_ms < 2000,
          "Start %lld ms, Stop %lld ms, Close %lld ms", start_ms, stop_ms, close_ms);
}

// Close from inside the event callback, on the controller thread: (a) in StateReady instead of
// starting; (b) in the StateStop of a natural end. The test thread stops touching the player once
// the callback has closed it, and the child must stay alive afterwards.
void CloseFromEventCallback() {
    {
        Options options;
        options.start_on_ready = false;
        Session* self = nullptr;
        std::atomic<bool> closed{false};
        std::atomic<long long> close_ms{0};
        std::atomic<s32> close_result{1};
        options.on_event = [&](AvPlayerEvents id) {
            if (id != AvPlayerEvents::StateReady) return;
            long long ms = 0;
            close_result = Timed(ms, [&] { return sceAvPlayerClose(self->handle); });
            close_ms = ms;
            closed = true;
        };
        Session s(options);
        self = &s;
        Step("(a) AddSource tiny_v20, Close in StateReady");
        CHECK(Open(s, Data("tiny_v20.mp4")) == 0, "(a) AddSource failed");
        for (int waited = 0; !closed && waited < 2000; ++waited) SleepMs(1);
        SleepMs(300);
        std::printf("  (a) Close in StateReady: %#x in %lld ms\n", close_result.load(),
                    close_ms.load());
        CHECK(closed, "(a) the callback did not finish Close within 2 s");
        CHECK(close_result == 0 && close_ms < 2000, "(a) Close %#x in %lld ms",
              close_result.load(), close_ms.load());
    }
    {
        Options options;
        options.sync = AvPlayerAvSyncMode::None;
        Session* self = nullptr;
        std::mutex use; // the test thread calls into the player only while holding it
        bool closed = false;
        long long close_ms = 0;
        s32 close_result = 1;
        options.on_event = [&](AvPlayerEvents id) {
            if (id != AvPlayerEvents::StateStop) return;
            std::lock_guard lock(use);
            if (closed) return;
            close_result = Timed(close_ms, [&] { return sceAvPlayerClose(self->handle); });
            closed = true;
        };
        Session s(options);
        self = &s;
        Step("(b) AddSource tiny_av, Close in StateStop");
        CHECK(Open(s, Data("tiny_av.mp4")) == 0, "(b) AddSource failed");
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "(b) no Start within 2 s");
        Step("(b) draining until the callback closes the player");
        const auto begin = Clock::now();
        bool inactive = false;
        while (MsSince(begin) < 5000) {
            {
                std::lock_guard lock(use);
                if (closed) break;
                AvPlayerFrameInfo info{};
                sceAvPlayerGetVideoData(s.handle, &info);
                sceAvPlayerGetAudioData(s.handle, &info);
                inactive = !sceAvPlayerIsActive(s.handle);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        SleepMs(300);
        std::lock_guard lock(use);
        std::printf("  (b) inactive %d; events %s; Close in StateStop: %#x in %lld ms\n",
                    int(inactive), s.EventList().c_str(), close_result, close_ms);
        CHECK(closed, "(b) no Close from StateStop within 5 s");
        CHECK(close_result == 0 && close_ms < 2000, "(b) Close %#x in %lld ms", close_result,
              close_ms);
    }
}

void RepeatedOpenClose() {
    const auto cycle = [](int index) {
        Session s;
        const auto begin = Clock::now();
        Step("cycle %d: AddSource", index);
        CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "cycle %d: AddSource failed", index);
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "cycle %d: no Start within 2 s", index);
        long long stop_ms = 0, close_ms = 0;
        if (index % 2 == 0) {
            Step("cycle %d: Stop", index);
            Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
        }
        Step("cycle %d: Close", index);
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        CHECK(MsSince(begin) < 2000, "cycle %d took %lld ms (Stop %lld, Close %lld)", index,
              MsSince(begin), stop_ms, close_ms);
    };
    cycle(-1); // warm-up: FFmpeg and the logger set up their state once
    SleepMs(200);
    const int base = ThreadCount();
    const long base_allocations = g_live_allocations;
    for (int index = 0; index < 200; ++index) cycle(index);
    int threads = ThreadCount();
    for (int waited = 0; threads > base + 1 && waited < 1000; waited += 10) {
        SleepMs(10);
        threads = ThreadCount();
    }
    std::printf("  threads %d before, %d after; guest allocations alive %ld before, %ld after\n",
                base, threads, base_allocations, g_live_allocations.load());
    CHECK(threads <= base + 1, "%d threads left over", threads - base);
}

void MissingGarbageAndTruncatedStream() {
    {
        Session s;
        Step("missing file");
        const s32 add = Open(s, Data("does_not_exist.mp4"));
        long long close_ms = 0;
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        std::printf("  missing: AddSource %#x, Close in %lld ms\n", add, close_ms);
        CHECK(add < 0, "AddSource of a missing file returned %#x", add);
        CHECK(close_ms < 2000, "missing: Close %lld ms", close_ms);
    }
    {
        char path[] = "/tmp/avplayer-test-garbage-XXXXXX.mp4";
        const int fd = mkstemps(path, 4);
        CHECK(fd >= 0, "mkstemps failed");
        u8 bytes[1024];
        u32 seed = 12345;
        for (u8& byte : bytes) byte = u8((seed = seed * 1103515245 + 12345) >> 16);
        CHECK(write(fd, bytes, sizeof(bytes)) == ssize_t(sizeof(bytes)), "write failed");
        close(fd);
        Session s;
        Step("1 KiB of garbage");
        const s32 add = Open(s, path);
        SleepMs(200); // an accepted source would fail later, on the controller thread
        long long close_ms = 0;
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        unlink(path);
        std::printf("  garbage: AddSource %#x, events %s, Close in %lld ms\n", add,
                    s.EventList().c_str(), close_ms);
        CHECK(add < 0, "AddSource of garbage returned %#x", add);
        CHECK(close_ms < 2000, "garbage: Close %lld ms", close_ms);
    }
    {
        struct stat st {};
        stat(Data("tiny_v150.mp4").c_str(), &st);
        Options options;
        options.read_fail_from = st.st_size / 2; // the moov is in the first 1.4 KB
        Session s(options);
        Step("reads fail from the middle of tiny_v150");
        CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "truncated: AddSource failed");
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "truncated: no Start within 2 s");
        Video video;
        const bool ended = Drain(s, video, 8000);
        const int warnings = s.Count(AvPlayerEvents::WarningId);
        long long stop_ms = 0, close_ms = 0;
        Step("truncated: Stop");
        const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
        Step("truncated: Close");
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        std::printf("  truncated: %s; inactive %d at %lld ms; events %s; Stop %#x in %lld ms, "
                    "Close in %lld ms\n",
                    Describe(video).c_str(), int(ended), video.inactive_ms, s.EventList().c_str(),
                    stop, stop_ms, close_ms);
        CHECK(ended || warnings > 0, "truncated: IsActive() still true after 8 s and no error");
        CHECK(stop_ms < 2000 && close_ms < 2000, "truncated: Stop %lld ms, Close %lld ms", stop_ms,
              close_ms);
    }
}

// ---- JumpToTime (the game jumps to CurrentTime() + ~1 s and stops, docs/avplayer.md §1) ----

// The first frame GetVideoData hands out within budget_ms, polled like Drain.
bool NextFrame(Session& s, AvPlayerFrameInfo& frame, int budget_ms, long long& waited_ms) {
    const auto begin = Clock::now();
    bool got = false;
    while (!(got = sceAvPlayerGetVideoData(s.handle, &frame)) && MsSince(begin) < budget_ms) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    waited_ms = MsSince(begin);
    return got;
}

// Whether a frame with this timestamp is the one on screen at `target` (it starts at most one frame
// before). Timestamps are rounded to the millisecond.
bool OnScreenAt(u64 timestamp, u64 target) {
    return timestamp <= target && double(timestamp) + kFrameMs + 1 >= double(target);
}

// A looping clip plays again after its end (the game loops titlebg1.mp4): frames keep coming with
// their timestamps wrapping around, and WAR_LOOPING_BACK comes once per loop of the demuxer, which
// runs at most 30 packets (1.5 loops of tiny_v20) ahead of what is shown. A jump after many loops
// still lands where asked.
void LoopsBack() {
    Options options;
    options.looping = true;
    Session s(options);
    Step("AddSource tiny_v20, looping");
    CHECK(Open(s, Data("tiny_v20.mp4")) == 0, "AddSource failed");
    CHECK(s.WaitStarted(2000) && s.start_result == 0, "no Start within 2 s");
    Video video;
    Step("playing 2 s");
    const bool ended = Drain(s, video, 2000);
    const int loops = s.CountWarning(kLoopingBack);
    Step("JumpToTime after the loops");
    const s32 jump = sceAvPlayerJumpToTime(s.handle, s.duration_ms * 5 + 500);
    AvPlayerFrameInfo first{};
    long long first_ms = 0, stop_ms = 0, close_ms = 0;
    const bool got = NextFrame(s, first, 1000, first_ms);
    Step("Stop");
    const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
    Step("Close");
    Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
    std::printf("  %s; %d wraps, %d WAR_LOOPING_BACK in 2 s of a %llu ms clip\n",
                Describe(video).c_str(), video.backwards, loops, (unsigned long long)s.duration_ms);
    std::printf("  then JumpToTime(+5 loops + 500) %#x, first frame ts %llu after %lld ms; Stop %#x "
                "in %lld ms, Close in %lld ms\n",
                jump, (unsigned long long)first.timestamp, first_ms, stop, stop_ms, close_ms);
    CHECK(!ended, "IsActive() turned false in a looping clip");
    CHECK(video.backwards >= 2, "%d frames and %d wraps in 2 s", video.frames, video.backwards);
    CHECK(loops <= video.backwards + 3, "%d WAR_LOOPING_BACK for %d wraps", loops,
          video.backwards);
    CHECK(jump == 0, "JumpToTime after the loops returned %#x", jump);
    CHECK(got && OnScreenAt(first.timestamp, 500),
          "first frame after the jump: %s, ts %llu (expected the one on screen at 500)",
          got ? "got" : "none", (unsigned long long)first.timestamp);
    CHECK(stop_ms < 2000 && close_ms < 2000, "Stop %lld ms, Close %lld ms", stop_ms, close_ms);
}

// A looping clip without audio keeps its pace after the first loop (the title menu loops
// titlebg1.mp4, video only, 120 s a loop): each frame comes no earlier than its timestamp plus the
// loops before it, and CurrentTime() is the time within the current loop. The clock of such a clip
// was the wall time since Start capped at the duration, so after the first loop it stayed at the
// duration and every later frame came as soon as it was asked for: in the game each loop of
// titlebg1 took 36 s from the second one on (Spec 5, task 7).
void LoopsBackOnTheClock() {
    Options options;
    options.looping = true;
    Session s(options);
    Step("AddSource tiny_v20, looping");
    CHECK(Open(s, Data("tiny_v20.mp4")) == 0, "AddSource failed");
    CHECK(s.WaitStarted(2000) && s.start_result == 0, "no Start within 2 s");
    const long long loop_ms = (long long)s.duration_ms;
    int frames = 0, wraps = 0;
    u64 last = 0, time = 0;
    long long max_early = LLONG_MIN, time_at = -1;
    Step("playing 2.5 s");
    const auto begin = Clock::now();
    while (MsSince(begin) < 2500) {
        AvPlayerFrameInfo info{};
        if (sceAvPlayerGetVideoData(s.handle, &info)) {
            wraps += frames > 0 && info.timestamp < last;
            last = info.timestamp;
            ++frames;
            const long long due = (long long)info.timestamp + wraps * loop_ms;
            max_early = std::max(max_early, due - MsSince(s.StartTime()));
        }
        if (time_at < 0 && MsSince(s.StartTime()) >= 1500) {
            time_at = MsSince(s.StartTime());
            time = sceAvPlayerCurrentTime(s.handle);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    const long long played = MsSince(s.StartTime());
    Step("Stop");
    const s32 stop = sceAvPlayerStop(s.handle);
    Step("Close");
    sceAvPlayerClose(s.handle);
    const long long expected = std::llround(double(played) / kFrameMs);
    std::printf("  %d frames and %d wraps in %lld ms of a %lld ms clip (about %lld on the clock); a frame "
                "came at most %lld ms early; CurrentTime %llu at %lld ms (expected about %lld)\n",
                frames, wraps, played, loop_ms, expected, max_early, (unsigned long long)time,
                time_at, time_at % loop_ms);
    CHECK(frames <= expected + 3, "%d frames in %lld ms, at most %lld on the clock", frames, played,
          expected + 3);
    CHECK(frames >= expected - 10, "%d frames in %lld ms: the loop stalled (about %lld expected)",
          frames, played, expected);
    CHECK(wraps >= 2, "%d wraps in %lld ms", wraps, played);
    CHECK(max_early <= 50, "a frame came %lld ms before its time", max_early);
    CHECK(std::llabs((long long)time - time_at % loop_ms) <= 50,
          "CurrentTime %llu at %lld ms, expected about %lld (the time within the loop)",
          (unsigned long long)time, time_at, time_at % loop_ms);
    CHECK(stop == 0, "Stop returned %#x", stop);
}

// A jump in the middle of tiny_v150, then the rest of the clip. Frames resume at the one on screen
// at the target although FFmpeg decodes from the keyframe before it (0: one GOP), none is lost after
// it, the clock follows the jump and the game is told the jump is complete.
void JumpThenStop() {
    constexpr u64 kTarget = 1000;
    Session s;
    Step("AddSource tiny_v150");
    CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "AddSource failed");
    CHECK(s.WaitStarted(2000) && s.start_result == 0, "no Start within 2 s (events %s)",
          s.EventList().c_str());
    Video before;
    Step("playing 300 ms");
    Drain(s, before, 300);
    long long jump_ms = 0, first_ms = 0;
    Step("JumpToTime(%llu)", (unsigned long long)kTarget);
    const s32 jump = Timed(jump_ms, [&] { return sceAvPlayerJumpToTime(s.handle, kTarget); });
    const u64 time = sceAvPlayerCurrentTime(s.handle);
    AvPlayerFrameInfo first{};
    Step("first frame after the jump");
    const bool got = NextFrame(s, first, 1000, first_ms);
    Video rest;
    Step("playing to the end");
    const bool ended = Drain(s, rest, 8000);
    Step("waiting for StateStop before Stop");
    const bool stop_event = s.WaitEvent(AvPlayerEvents::StateStop, 1000);
    long long stop_ms = 0, close_ms = 0;
    Step("Stop");
    const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
    Step("Close");
    const s32 closed = Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
    const int expected = 150 - int(std::lround(double(first.timestamp) / kFrameMs));
    std::printf("  %d frames before (last ts %llu); JumpToTime %#x in %lld ms, then CurrentTime %llu; "
                "first frame ts %llu after %lld ms; then %s\n",
                before.frames, (unsigned long long)before.last_ts, jump, jump_ms,
                (unsigned long long)time, (unsigned long long)first.timestamp, first_ms,
                Describe(rest).c_str());
    std::printf("  events %s; Stop %#x in %lld ms, Close %#x in %lld ms\n", s.EventList().c_str(),
                stop, stop_ms, closed, close_ms);
    CHECK(jump == 0, "JumpToTime returned %#x", jump);
    CHECK(jump_ms < 500, "JumpToTime took %lld ms", jump_ms);
    CHECK(time >= kTarget && time < kTarget + 100, "CurrentTime() %llu right after the jump",
          (unsigned long long)time);
    CHECK(got && first_ms < 300, "no frame within 300 ms of the jump");
    CHECK(OnScreenAt(first.timestamp, kTarget), "first frame after the jump has ts %llu",
          (unsigned long long)first.timestamp);
    CHECK(ended, "IsActive() still true 8 s after the jump");
    CHECK(1 + rest.frames == expected && rest.backwards == 0,
          "%d frames from ts %llu to the end (%d backwards), expected %d", 1 + rest.frames,
          (unsigned long long)first.timestamp, rest.backwards, expected);
    CHECK(s.WaitWarning(kJumpComplete, 0), "no WarningId %#x (jump complete)", kJumpComplete);
    CHECK(stop_event, "no StateStop within 1 s of the end, before Stop");
    CHECK(stop_ms < 2000 && close_ms < 2000, "Stop %lld ms, Close %lld ms", stop_ms, close_ms);
}

// How the game leaves titlebg1.mp4 and TM_CI.mp4 early: CurrentTime, JumpToTime(+1 s) and Stop in
// the same millisecond, a second Stop once IsActive() is false, Close. Video only and with audio, at
// varying points of the clip.
void JumpThenStopLikeTheGame() {
    long long max_jump = 0, max_stop = 0, max_cycle = 0;
    for (int cycle = 0; cycle < 20; ++cycle) {
        const bool with_audio = cycle % 2 == 1;
        Session s;
        const auto begin = Clock::now();
        Step("cycle %d: AddSource", cycle);
        CHECK(Open(s, Data(with_audio ? "tiny_av.mp4" : "tiny_v150.mp4")) == 0,
              "cycle %d: AddSource failed", cycle);
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "cycle %d: no Start within 2 s", cycle);
        std::optional<AudioConsumer> audio;
        if (with_audio) audio.emplace(s.handle);
        Video video;
        Step("cycle %d: playing", cycle);
        Drain(s, video, 200 + cycle * 37 % 400);
        long long jump_ms = 0, stop_ms = 0;
        Step("cycle %d: JumpToTime and Stop", cycle);
        const u64 target = sceAvPlayerCurrentTime(s.handle) + 1000;
        const s32 jump = Timed(jump_ms, [&] { return sceAvPlayerJumpToTime(s.handle, target); });
        const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
        const bool active = sceAvPlayerIsActive(s.handle);
        Step("cycle %d: second Stop", cycle);
        const s32 second = sceAvPlayerStop(s.handle);
        if (audio) audio->Join();
        Step("cycle %d: Close", cycle);
        sceAvPlayerClose(s.handle);
        const long long cycle_ms = MsSince(begin);
        CHECK(jump == 0, "cycle %d: JumpToTime(%llu) returned %#x", cycle,
              (unsigned long long)target, jump);
        CHECK(stop == 0 && !active && second == kOperationFailed,
              "cycle %d: Stop %#x, then IsActive %d, second Stop %#x", cycle, stop, int(active),
              second);
        CHECK(cycle_ms < 2000, "cycle %d took %lld ms (JumpToTime %lld, Stop %lld)", cycle,
              cycle_ms, jump_ms, stop_ms);
        max_jump = std::max(max_jump, jump_ms);
        max_stop = std::max(max_stop, stop_ms);
        max_cycle = std::max(max_cycle, cycle_ms);
    }
    std::printf("  20 cycles; JumpToTime <= %lld ms, Stop <= %lld ms, cycle <= %lld ms\n", max_jump,
                max_stop, max_cycle);
}

// Edges of JumpToTime: (a) past the end of a clip, which then ends with its StateStop; (b) past the
// end of a looping clip, where the time wraps around (titlebg1.mp4 loops, and the game jumps +1 s
// from wherever it is); (c) with nothing playing, before Start or after Stop, which must fail.
void JumpPastTheEndAndWhenStopped() {
    {
        Session s;
        Step("(a) AddSource tiny_v150");
        CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "(a) AddSource failed");
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "(a) no Start within 2 s");
        Video before;
        Drain(s, before, 300);
        Step("(a) JumpToTime past the end");
        const s32 jump = sceAvPlayerJumpToTime(s.handle, 60000);
        const auto jumped = Clock::now();
        Video rest;
        const bool ended = Drain(s, rest, 1000);
        const long long ended_ms = MsSince(jumped);
        const bool stop_event = s.WaitEvent(AvPlayerEvents::StateStop, 1000);
        long long stop_ms = 0, close_ms = 0;
        Step("(a) Stop");
        const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
        const s32 again = sceAvPlayerJumpToTime(s.handle, 1000);
        Step("(a) Close");
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        std::printf("  (a) past the end: JumpToTime %#x; then %s, inactive after %lld ms; events %s; "
                    "Stop %#x in %lld ms; JumpToTime after Stop %#x\n",
                    jump, Describe(rest).c_str(), ended_ms, s.EventList().c_str(), stop, stop_ms,
                    again);
        CHECK(jump == 0, "(a) JumpToTime past the end returned %#x", jump);
        CHECK(ended, "(a) IsActive() still true 1 s after jumping past the end");
        CHECK(stop_event, "(a) no StateStop within 1 s of the end, before Stop");
        CHECK(again == kOperationFailed, "(a) JumpToTime after Stop returned %#x", again);
        CHECK(stop_ms < 2000 && close_ms < 2000, "(a) Stop %lld ms, Close %lld ms", stop_ms,
              close_ms);
    }
    {
        Options options;
        options.looping = true;
        Session s(options);
        Step("(b) AddSource tiny_v150, looping");
        CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "(b) AddSource failed");
        CHECK(s.WaitStarted(2000) && s.start_result == 0, "(b) no Start within 2 s");
        Video before;
        Drain(s, before, 300);
        const u64 target = s.duration_ms * 3 + 1000;
        Step("(b) JumpToTime past the end of a looping clip");
        const s32 jump = sceAvPlayerJumpToTime(s.handle, target);
        AvPlayerFrameInfo first{};
        long long first_ms = 0, stop_ms = 0, close_ms = 0;
        const bool got = NextFrame(s, first, 1000, first_ms);
        Step("(b) Stop");
        Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
        Step("(b) Close");
        Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
        std::printf("  (b) looping, %llu ms long: JumpToTime(%llu) %#x; first frame ts %llu after "
                    "%lld ms\n",
                    (unsigned long long)s.duration_ms, (unsigned long long)target, jump,
                    (unsigned long long)first.timestamp, first_ms);
        CHECK(jump == 0, "(b) JumpToTime returned %#x", jump);
        CHECK(got && OnScreenAt(first.timestamp, 1000),
              "(b) first frame after the jump: %s, ts %llu (expected the one on screen at 1000)",
              got ? "got" : "none", (unsigned long long)first.timestamp);
        CHECK(stop_ms < 2000 && close_ms < 2000, "(b) Stop %lld ms, Close %lld ms", stop_ms,
              close_ms);
    }
    {
        Options options;
        options.start_on_ready = false;
        Session s(options);
        Step("(c) AddSource tiny_v20, no Start");
        CHECK(Open(s, Data("tiny_v20.mp4")) == 0, "(c) AddSource failed");
        CHECK(s.WaitEvent(AvPlayerEvents::StateReady, 2000), "(c) no StateReady within 2 s");
        const s32 jump = sceAvPlayerJumpToTime(s.handle, 300);
        Step("(c) Close");
        sceAvPlayerClose(s.handle);
        std::printf("  (c) before Start: JumpToTime %#x\n", jump);
        CHECK(jump == kOperationFailed, "(c) JumpToTime before Start returned %#x", jump);
    }
}

// ---- Runner ----

struct Case {
    const char* name;
    int deadline_s;
    void (*run)();
    bool broken_input = false; // the decoder may complain about the stream
};

std::string ReadAll(int fd) {
    std::string text;
    char buffer[4096];
    lseek(fd, 0, SEEK_SET);
    for (ssize_t n; (n = read(fd, buffer, sizeof(buffer))) > 0;) text.append(buffer, size_t(n));
    return text;
}

std::string ReadLine(const std::string& path) {
    std::string line;
    if (FILE* file = std::fopen(path.c_str(), "r")) {
        char buffer[128] = {};
        if (std::fgets(buffer, sizeof(buffer), file)) line = buffer;
        std::fclose(file);
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == ' ')) line.pop_back();
    return line;
}

// Thread names and kernel wait channels of a hung child, before it is killed.
std::string Threads(pid_t pid) {
    std::string list;
    const std::string task = "/proc/" + std::to_string(pid) + "/task";
    if (DIR* dir = opendir(task.c_str())) {
        while (const dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.') continue;
            const std::string base = task + "/" + entry->d_name;
            list += (list.empty() ? "" : ", ") + ReadLine(base + "/comm") + " (" +
                    ReadLine(base + "/wchan") + ")";
        }
        closedir(dir);
    }
    return list;
}

// gdb's stacks of every thread of a hung child (AVPLAYER_TEST_GDB=1), frames only. Attaching
// interrupts futex waits, so the child may move on before it is killed.
std::string Stacks(pid_t pid) {
    const std::string command = "gdb -q -batch -p " + std::to_string(pid) +
                                " -ex 'thread apply all bt 16' 2>/dev/null";
    std::string stacks;
    if (FILE* pipe = popen(command.c_str(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof(line), pipe)) {
            if (line[0] == '#' || std::strncmp(line, "Thread ", 7) == 0) stacks += "  gdb: " + std::string(line);
        }
        pclose(pipe);
    }
    return stacks;
}

int Count(const std::string& text, const char* needle) {
    int count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        ++count;
    }
    return count;
}

// What became of one forked child; an empty reason means it passed.
struct Outcome {
    std::string reason;
    long long ms = 0;
    bool hung = false;
    std::string threads, stacks; // of a hung child, before it was killed
    std::string errors;          // its stderr
};

Outcome RunChild(const std::string& name, int deadline_s, const std::function<void()>& run,
                 bool broken_input) {
    Outcome outcome;
    char path[] = "/tmp/avplayer-test-stderr-XXXXXX";
    const int err = mkstemp(path);
    if (err < 0) {
        outcome.reason = "mkstemp";
        return outcome;
    }
    unlink(path);
    std::snprintf(g_progress->step, sizeof(g_progress->step), "start");
    g_progress->failure[0] = '\0';
    g_progress->frames = 0;
    g_progress->blocks = 0;
    g_progress->end_ms = -1;
    g_progress->late_ms = 0;
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(err, 2);
        close(err);
        setvbuf(stdout, nullptr, _IOLBF, 0);
        run();
        std::fflush(stdout);
        std::_Exit(0);
    }
    const auto begin = Clock::now();
    auto last_step = begin;
    unsigned steps = g_progress->steps;
    int status = 0;
    std::string step;
    long long stalled_ms = 0;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (g_progress->steps != steps) {
            steps = g_progress->steps;
            last_step = Clock::now();
        }
        if (MsSince(last_step) > kStallMs || MsSince(begin) > deadline_s * 1000LL) {
            step = g_progress->step;
            stalled_ms = MsSince(last_step);
            outcome.hung = true;
            outcome.threads = Threads(pid);
            const char* gdb = std::getenv("AVPLAYER_TEST_GDB");
            if (gdb != nullptr && gdb[0] == '1') outcome.stacks = Stacks(pid);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        SleepMs(20);
    }
    outcome.ms = MsSince(begin);
    outcome.errors = ReadAll(err);
    close(err);
    if (const char* keep = std::getenv("AVPLAYER_TEST_KEEP")) {
        if (FILE* file = std::fopen((std::string(keep) + "/" + name + ".stderr").c_str(), "w")) {
            std::fwrite(outcome.errors.data(), 1, outcome.errors.size(), file);
            std::fclose(file);
        }
    }
    const int join_failed = Count(outcome.errors, "join failed");
    const int decoder_errors = broken_input ? 0 : Count(outcome.errors, "[h264 @");
    char reason[512] = "";
    if (outcome.hung) {
        std::snprintf(reason, sizeof(reason), "hung: no progress for %lld s in \"%s\"",
                      stalled_ms / 1000, step.c_str());
    } else if (WIFSIGNALED(status)) {
        std::snprintf(reason, sizeof(reason), "killed by signal %d (%s) in \"%s\"",
                      WTERMSIG(status), strsignal(WTERMSIG(status)), g_progress->step);
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::snprintf(reason, sizeof(reason), "check failed");
    } else if (join_failed > 0) {
        std::snprintf(reason, sizeof(reason), "%d \"join failed\" on stderr", join_failed);
    } else if (decoder_errors > 0) {
        std::snprintf(reason, sizeof(reason), "%d H.264 decoder errors on stderr", decoder_errors);
    }
    outcome.reason = reason;
    return outcome;
}

// A failed child's threads, if it hung, and the tail of its stderr: AvPlayer's own warnings and
// errors.
void PrintDetails(const Outcome& outcome) {
    if (outcome.hung) std::printf("  threads: %s\n%s", outcome.threads.c_str(), outcome.stacks.c_str());
    const std::string& errors = outcome.errors;
    std::vector<std::string> lines;
    for (size_t at = 0; at < errors.size();) {
        const size_t end = errors.find('\n', at);
        lines.push_back(errors.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? errors.size() : end + 1;
    }
    for (size_t index = lines.size() > 12 ? lines.size() - 12 : 0; index < lines.size(); ++index) {
        std::printf("  stderr: %s\n", lines[index].c_str());
    }
}

bool RunIsolated(const Case& test) {
    std::printf("-- %s\n", test.name);
    const Outcome outcome = RunChild(test.name, test.deadline_s, test.run, test.broken_input);
    if (outcome.reason.empty()) {
        std::printf("PASS: %s (%lld ms)\n", test.name, outcome.ms);
        return true;
    }
    std::printf("FAIL: %s: %s (%lld ms)\n", test.name, outcome.reason.c_str(), outcome.ms);
    PrintDetails(outcome);
    return false;
}

// ---- Sweep of the game's videos (--movie-dir, plan task 5) ----

// One video played the way the game plays it, for at most `seconds`: every stream enabled and
// started from StateReady, the video drained here and the audio by an "av audio" thread, then Stop
// and Close. It needs a frame, an audio block if there is an audio stream, timestamps that never
// go back and, if the video ended in time, its last frame: 75 of the 134 videos have B-frames,
// which the fixtures do not, and the decoder holds those frames back. The counts go to the parent
// through g_progress.
void PlayMovie(const std::string& path, int seconds) {
    Session s;
    Step("AddSource");
    const s32 added = Open(s, path);
    CHECK(added == 0, "AddSource %#x", added);
    CHECK(s.WaitStarted(5000) && s.start_result == 0, "no Start within 5 s (events %s)",
          s.EventList().c_str());
    std::optional<AudioConsumer> audio;
    if (s.has_audio) audio.emplace(s.handle);
    Video video;
    bool ended = false;
    // In one-second slices, so that a long window is not taken for a stall.
    for (int second = 0; second < seconds && !ended; ++second) {
        Step("playing, second %d", second);
        ended = Drain(s, video, 1000, audio ? &*audio : nullptr);
    }
    g_progress->frames = video.frames;
    g_progress->late_ms = video.frames > 0 ? video.max_late : 0;
    g_progress->end_ms = ended ? video.inactive_ms : -1;
    long long stop_ms = 0, close_ms = 0;
    Step("Stop");
    const s32 stop = Timed(stop_ms, [&] { return sceAvPlayerStop(s.handle); });
    if (audio) {
        audio->Join();
        g_progress->blocks = audio->Blocks();
    }
    Step("Close");
    Timed(close_ms, [&] { return sceAvPlayerClose(s.handle); });
    CHECK(video.frames >= 1, "no video frame in %d s", seconds);
    CHECK(!s.has_audio || g_progress->blocks >= 1, "no audio block in %d s", seconds);
    CHECK(video.backwards == 0, "%d frames went back in time", video.backwards);
    // The last frame starts one frame before the end: 50 ms is one frame at 30 fps, not two.
    CHECK(!ended || video.last_ts + 50 >= s.video_duration_ms,
          "ended with the frame at %llu ms of a %llu ms video", (unsigned long long)video.last_ts,
          (unsigned long long)s.video_duration_ms);
    CHECK(ended || stop == 0, "Stop during playback returned %#x", stop);
    CHECK(stop_ms < 2000 && close_ms < 2000, "Stop %lld ms, Close %lld ms", stop_ms, close_ms);
}

// PlayMovie on every .mp4 of dir (or on the ones named), each in its own child: one line per
// video, "OK|FAIL <file> v=<frames> a=<audio blocks> ms=<run> [end=<ms after Start>] late=<ms>",
// where late is how far the latest frame came behind its timestamp. A failure does not stop the
// rest.
int Sweep(const std::string& dir, int seconds, const std::vector<std::string>& only) {
    std::vector<std::string> movies;
    if (DIR* listing = opendir(dir.c_str())) {
        while (const dirent* entry = readdir(listing)) {
            const std::string name = entry->d_name;
            const bool mp4 = name.size() > 4 && name.compare(name.size() - 4, 4, ".mp4") == 0;
            if (mp4 && (only.empty() || std::find(only.begin(), only.end(), name) != only.end())) {
                movies.push_back(name);
            }
        }
        closedir(listing);
    }
    std::sort(movies.begin(), movies.end());
    if (movies.empty()) {
        std::fprintf(stderr, "no .mp4 to play in %s\n", dir.c_str());
        return 2;
    }
    g_sweep = true;
    std::string failed;
    int failures = 0;
    for (const auto& name : movies) {
        const std::string path = dir + "/" + name;
        const Outcome outcome =
            RunChild(name, seconds + 30, [&path, seconds] { PlayMovie(path, seconds); }, false);
        const bool ok = outcome.reason.empty();
        std::printf("%s %s v=%d a=%d ms=%lld", ok ? "OK" : "FAIL", name.c_str(),
                    g_progress->frames.load(), g_progress->blocks.load(), outcome.ms);
        if (g_progress->end_ms >= 0) std::printf(" end=%lld", g_progress->end_ms.load());
        std::printf(" late=%lld", g_progress->late_ms.load());
        if (!ok) {
            std::printf(": %s", outcome.reason.c_str());
            if (g_progress->failure[0] != '\0') std::printf(": %s", g_progress->failure);
        }
        std::printf("\n");
        std::fflush(stdout);
        if (!ok) {
            PrintDetails(outcome);
            failed += " " + name;
            ++failures;
        }
    }
    std::printf("%d of %zu videos failed%s\n", failures, movies.size(), failed.c_str());
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const auto usage = [argv] {
        std::fprintf(stderr,
                     "usage: %s <data dir> [case ...]\n"
                     "       %s --movie-dir <dir> [--seconds N] [file.mp4 ...]\n",
                     argv[0], argv[0]);
        return 2;
    };
    std::string movie_dir;
    int seconds = 3, first = 2;
    if (argc >= 2 && std::strcmp(argv[1], "--movie-dir") == 0) {
        if (argc < 3) return usage();
        movie_dir = argv[2];
        first = 3;
        if (argc >= 5 && std::strcmp(argv[3], "--seconds") == 0) {
            seconds = std::atoi(argv[4]);
            first = 5;
        }
        if (seconds < 1) return usage();
    } else if (argc < 2) {
        return usage();
    } else {
        g_data = argv[1];
        if (access(Data("tiny_v150.mp4").c_str(), R_OK) != 0) {
            std::fprintf(stderr, "missing fixture %s\n", Data("tiny_v150.mp4").c_str());
            return 2;
        }
    }
    void* shared = mmap(nullptr, sizeof(Progress), PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) return 2;
    g_progress = new (shared) Progress{};
    if (ThreadCount() != 1) {
        std::printf("warning: %d threads before fork\n", ThreadCount());
    }
    const std::vector<std::string> only(argv + first, argv + argc);
    if (!movie_dir.empty()) return Sweep(movie_dir, seconds, only);
    const Case cases[] = {
        {"plays_to_end_video_only", 20, PlaysToEndVideoOnly},
        {"plays_to_end_with_b_frames", 20, PlaysToEndWithBFrames},
        {"plays_to_end_with_audio", 20, PlaysToEndWithAudio},
        {"eof_then_stop_trigger", 300, EofThenStopTrigger},
        {"eof_with_undrained_buffers_then_stop", 60, EofWithUndrainedBuffersThenStop},
        {"stop_during_playback_without_draining", 40, StopDuringPlaybackWithoutDraining},
        {"stop_right_after_start_and_twice", 60, StopRightAfterStartAndTwice},
        {"start_while_playing", 15, StartWhilePlaying},
        {"close_from_event_callback", 20, CloseFromEventCallback},
        {"repeated_open_close", 120, RepeatedOpenClose},
        {"missing_garbage_and_truncated_stream", 30, MissingGarbageAndTruncatedStream, true},
        {"loops_back", 20, LoopsBack},
        {"loops_back_on_the_clock", 20, LoopsBackOnTheClock},
        {"jump_then_stop", 20, JumpThenStop},
        {"jump_then_stop_like_the_game", 60, JumpThenStopLikeTheGame},
        {"jump_past_the_end_and_when_stopped", 20, JumpPastTheEndAndWhenStopped},
    };
    int ran = 0, failed = 0;
    for (const auto& test : cases) {
        if (!only.empty() && std::find(only.begin(), only.end(), test.name) == only.end()) continue;
        ++ran;
        failed += !RunIsolated(test);
    }
    std::printf("%d of %d cases failed\n", failed, ran);
    return failed == 0 ? 0 : 1;
}
