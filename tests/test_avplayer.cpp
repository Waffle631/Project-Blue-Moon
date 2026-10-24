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
// (gpu/shim/core/libraries/kernel/threads.h) also fails it.
//
// Fixtures (tests/data, FFmpeg 8.1). They are H.264 Main without B-frames at 29.97 fps with the moov
// first, like image/movie/. tiny_v150 replicates phasetitle_01_00_en.mp4 (video only, 150 frames,
// 5.005 s).
//   V="-c:v libx264 -profile:v main -bf 0 -pix_fmt yuv420p -crf 42 -preset veryslow -map_metadata -1
//      -fflags +bitexact -flags:v +bitexact -movflags +faststart"
//   ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30000/1001 -frames:v 150 $V -an tiny_v150.mp4
//   ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30000/1001 -frames:v 20 $V -an tiny_v20.mp4
//   ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30000/1001 -f lavfi -i
//      sine=frequency=440:sample_rate=48000 -t 2 $V -c:a aac -ac 2 -ar 48000 -b:a 48k
//      -flags:a +bitexact tiny_av.mp4
//
// Usage: avplayer-test <data dir> [case ...]   (ninja -C out/gpu avplayer-test)
// AVPLAYER_TEST_GDB=1 adds gdb's stacks of a hung child to the report; AVPLAYER_TEST_KEEP=<dir> keeps
// each case's stderr in <dir>/<case>.stderr (run with GOW3_TRACE_AVPLAYER=1 to see every call).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
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
// Exported by libgow3gpu.so (avplayer.cpp); avplayer.h only declares RegisterLib.
s32 PS4_SYSV_ABI sceAvPlayerAddSource(AvPlayerHandle handle, const char* filename);
s32 PS4_SYSV_ABI sceAvPlayerClose(AvPlayerHandle handle);
s32 PS4_SYSV_ABI sceAvPlayerEnableStream(AvPlayerHandle handle, u32 stream_id);
bool PS4_SYSV_ABI sceAvPlayerGetAudioData(AvPlayerHandle handle, AvPlayerFrameInfo* p_info);
s32 PS4_SYSV_ABI sceAvPlayerGetStreamInfo(AvPlayerHandle handle, u32 stream_id,
                                          AvPlayerStreamInfo* p_info);
bool PS4_SYSV_ABI sceAvPlayerGetVideoData(AvPlayerHandle handle, AvPlayerFrameInfo* video_info);
AvPlayerHandle PS4_SYSV_ABI sceAvPlayerInit(AvPlayerInitData* data);
bool PS4_SYSV_ABI sceAvPlayerIsActive(AvPlayerHandle handle);
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

std::string g_data;

// Shared with the parent: the step the child is in, and how many steps it has taken.
struct Progress {
    std::atomic<unsigned> steps;
    char step[256];
};
Progress* g_progress = nullptr;
constexpr long long kStallMs = 15000;

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
    std::printf("  failed: ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
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
    std::function<void(AvPlayerEvents)> on_event; // on the controller thread, after the start
};

struct Event {
    AvPlayerEvents id;
    long long ms; // since Open
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

    Clock::time_point StartTime() {
        std::lock_guard lock(mutex);
        return start_time;
    }

    std::string EventList() {
        std::lock_guard lock(mutex);
        std::string list;
        for (const auto& event : events) {
            list += (list.empty() ? "" : " ") + std::string(EventName(event.id)) + "@" +
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
    u64 duration_ms = 0;
};

void PS4_SYSV_ABI OnEvent(void* object, AvPlayerEvents id, s32, void*) {
    auto* s = static_cast<Session*>(object);
    {
        std::lock_guard lock(s->mutex);
        s->events.push_back({id, MsSince(s->opened)});
    }
    s->cv.notify_all();
    if (id == AvPlayerEvents::StateReady && s->options.start_on_ready) {
        // What the game does in this callback, on the controller thread: every stream is enabled,
        // then Start and SetLooping (docs/avplayer.md §1).
        const s32 count = sceAvPlayerStreamCount(s->handle);
        bool has_audio = false;
        u64 duration = 0;
        for (s32 index = 0; index < count; ++index) {
            AvPlayerStreamInfo info{};
            if (sceAvPlayerGetStreamInfo(s->handle, u32(index), &info) != 0) continue;
            has_audio |= info.type == AvPlayerStreamType::Audio;
            duration = std::max(duration, info.duration);
            sceAvPlayerEnableStream(s->handle, u32(index));
        }
        const auto start_time = Clock::now();
        const s32 result = sceAvPlayerStart(s->handle);
        sceAvPlayerSetLooping(s->handle, false);
        {
            std::lock_guard lock(s->mutex);
            s->has_audio = has_audio;
            s->duration_ms = duration;
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

void PlaysToEndVideoOnly() {
    Session s;
    Step("AddSource tiny_v150");
    CHECK(Open(s, Data("tiny_v150.mp4")) == 0, "AddSource failed");
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
    CHECK(video.frames == 150, "%d of 150 frames", video.frames); // no B-frames: none held back
    CHECK(video.max_early <= 150, "a frame came %lld ms before its timestamp", video.max_early);
    CHECK(stop_event, "no StateStop within 1 s of the end, before Stop");
    CHECK(stop_ms < 2000 && close_ms < 2000, "Stop %lld ms, Close %lld ms", stop_ms, close_ms);
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

// ---- Runner ----

struct Case {
    const char* name;
    int deadline_s;
    void (*run)();
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

bool RunIsolated(const Case& test) {
    char path[] = "/tmp/avplayer-test-stderr-XXXXXX";
    const int err = mkstemp(path);
    if (err < 0) {
        std::printf("FAIL: %s (mkstemp)\n", test.name);
        return false;
    }
    unlink(path);
    std::snprintf(g_progress->step, sizeof(g_progress->step), "start");
    std::printf("-- %s\n", test.name);
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(err, 2);
        close(err);
        setvbuf(stdout, nullptr, _IOLBF, 0);
        test.run();
        std::fflush(stdout);
        std::_Exit(0);
    }
    const auto begin = Clock::now();
    auto last_step = begin;
    unsigned steps = g_progress->steps;
    int status = 0;
    std::string hung, stacks, step;
    long long stalled_ms = 0;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (g_progress->steps != steps) {
            steps = g_progress->steps;
            last_step = Clock::now();
        }
        if (MsSince(last_step) > kStallMs || MsSince(begin) > test.deadline_s * 1000LL) {
            step = g_progress->step;
            stalled_ms = MsSince(last_step);
            hung = Threads(pid);
            const char* gdb = std::getenv("AVPLAYER_TEST_GDB");
            if (gdb != nullptr && gdb[0] == '1') stacks = Stacks(pid);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        SleepMs(20);
    }
    const long long ms = MsSince(begin);
    const std::string errors = ReadAll(err);
    close(err);
    if (const char* keep = std::getenv("AVPLAYER_TEST_KEEP")) {
        if (FILE* file = std::fopen((std::string(keep) + "/" + test.name + ".stderr").c_str(), "w")) {
            std::fwrite(errors.data(), 1, errors.size(), file);
            std::fclose(file);
        }
    }
    const int join_failed = Count(errors, "join failed");
    char reason[512] = "";
    if (!hung.empty()) {
        std::snprintf(reason, sizeof(reason), "hung: no progress for %lld s in \"%s\"",
                      stalled_ms / 1000, step.c_str());
    } else if (WIFSIGNALED(status)) {
        std::snprintf(reason, sizeof(reason), "killed by signal %d (%s) in \"%s\"",
                      WTERMSIG(status), strsignal(WTERMSIG(status)), g_progress->step);
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::snprintf(reason, sizeof(reason), "check failed");
    } else if (join_failed > 0) {
        std::snprintf(reason, sizeof(reason), "%d \"join failed\" on stderr", join_failed);
    }
    if (reason[0] == '\0') {
        std::printf("PASS: %s (%lld ms)\n", test.name, ms);
        return true;
    }
    std::printf("FAIL: %s: %s (%lld ms)\n", test.name, reason, ms);
    if (!hung.empty()) std::printf("  threads: %s\n%s", hung.c_str(), stacks.c_str());
    // The tail of the child's stderr: AvPlayer's own warnings and errors.
    std::vector<std::string> lines;
    for (size_t at = 0; at < errors.size();) {
        const size_t end = errors.find('\n', at);
        lines.push_back(errors.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? errors.size() : end + 1;
    }
    for (size_t index = lines.size() > 12 ? lines.size() - 12 : 0; index < lines.size(); ++index) {
        std::printf("  stderr: %s\n", lines[index].c_str());
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <data dir> [case ...]\n", argv[0]);
        return 2;
    }
    g_data = argv[1];
    if (access(Data("tiny_v150.mp4").c_str(), R_OK) != 0) {
        std::fprintf(stderr, "missing fixture %s\n", Data("tiny_v150.mp4").c_str());
        return 2;
    }
    void* shared = mmap(nullptr, sizeof(Progress), PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) return 2;
    g_progress = new (shared) Progress{};
    if (ThreadCount() != 1) {
        std::printf("warning: %d threads before fork\n", ThreadCount());
    }
    const Case cases[] = {
        {"plays_to_end_video_only", 20, PlaysToEndVideoOnly},
        {"plays_to_end_with_audio", 20, PlaysToEndWithAudio},
        {"eof_then_stop_trigger", 300, EofThenStopTrigger},
        {"eof_with_undrained_buffers_then_stop", 60, EofWithUndrainedBuffersThenStop},
        {"stop_during_playback_without_draining", 40, StopDuringPlaybackWithoutDraining},
        {"stop_right_after_start_and_twice", 60, StopRightAfterStartAndTwice},
        {"start_while_playing", 15, StartWhilePlaying},
        {"close_from_event_callback", 20, CloseFromEventCallback},
        {"repeated_open_close", 120, RepeatedOpenClose},
        {"missing_garbage_and_truncated_stream", 30, MissingGarbageAndTruncatedStream},
    };
    const std::vector<std::string> only(argv + 2, argv + argc);
    int ran = 0, failed = 0;
    for (const auto& test : cases) {
        if (!only.empty() && std::find(only.begin(), only.end(), test.name) == only.end()) continue;
        ++ran;
        failed += !RunIsolated(test);
    }
    std::printf("%d of %d cases failed\n", failed, ran);
    return failed == 0 ? 0 : 1;
}
