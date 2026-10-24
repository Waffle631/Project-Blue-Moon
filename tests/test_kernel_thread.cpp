// SPDX-License-Identifier: GPL-2.0-or-later
// Spec 5: Libraries::Kernel::Thread (gpu/shim/core/libraries/kernel/threads.h), the host thread behind
// AvPlayer's demuxer, decoders and controller. AvPlayer joins one decoder thread from two places, the
// demuxer at EOF and the game's sceAvPlayerStop (avplayer_source.cpp, DemuxerThread and Stop), and
// every one of those threads joins itself on its way out. With one std::jthread shared by all those
// calls, a second pthread_join of the same thread returned EINVAL ("join failed") or never returned:
// sceAvPlayerStop hung at the end of phasetitle_01_00_en.mp4 (docs/avplayer.md §3, tests/test_avplayer.cpp).
//
// fd 2 goes to a temporary file and a "join failed" line fails the case that printed it. A case that
// outlives its budget is reported and the program exits, since the old code could hang for good.
// Usage: kernel-thread-test [case ...]   (ninja -C out/gpu kernel-thread-test; run under timeout 60)
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <unistd.h>

#include "core/libraries/kernel/threads.h"

using Libraries::Kernel::Thread;
using Clock = std::chrono::steady_clock;

namespace {

long long MsSince(Clock::time_point begin) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count();
}

void SleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Spins up to timeout_ms for flag; returns its last value.
bool WaitFor(const std::atomic<bool>& flag, int timeout_ms) {
    const auto begin = Clock::now();
    while (!flag && MsSince(begin) < timeout_ms) std::this_thread::sleep_for(std::chrono::microseconds(50));
    return flag;
}

// ---- stderr capture: fd 2 appends to a file read back through its own handle ----

std::string g_stderr_path;

std::string CapturedStderr() {
    std::fflush(stderr);
    std::string text;
    if (FILE* file = std::fopen(g_stderr_path.c_str(), "rb")) {
        char buffer[4096];
        for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) text.append(buffer, n);
        std::fclose(file);
    }
    return text;
}

int JoinFailures(const std::string& text) {
    int count = 0;
    for (size_t at = 0; (at = text.find("join failed", at)) != std::string::npos; ++at) ++count;
    return count;
}

// ---- watchdog: a hung case cannot be stopped from inside, so the program reports it and exits ----

std::atomic<const char*> g_case{nullptr};
std::atomic<long long> g_deadline{0}; // ms since the program started
const auto g_begin = Clock::now();

void Watchdog() {
    for (;;) {
        SleepMs(10);
        const char* name = g_case.load();
        if (name != nullptr && MsSince(g_begin) > g_deadline.load()) {
            std::printf("FAIL: %s: still running after its budget (a join that never returns)\n", name);
            std::printf("  stderr: %s\n", g_stderr_path.c_str());
            std::fflush(stdout);
            std::_Exit(1);
        }
    }
}

// ---- cases ----

char g_failure[512];

bool Fail(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(g_failure, sizeof(g_failure), format, args);
    va_end(args);
    return false;
}

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) return Fail(__VA_ARGS__);                                                     \
    } while (0)

// The demuxer's pattern: at EOF the thread joins itself on its way out, and the game sees
// IsActive() turn false and calls Stop in the same poll (DemuxerThread, then sceAvPlayerStop).
// Stop must not come back while the function is still running.
bool SelfJoinVsImmediateStop() {
    for (int i = 0; i < 5000; ++i) {
        Thread t;
        std::atomic<bool> eof{false}, tail_done{false};
        t.Run([&](std::stop_token) {
            eof = true;
            t.Join();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            tail_done = true;
        });
        while (!eof) {
        }
        t.Stop();
        const bool done = tail_done;
        WaitFor(tail_done, 1000); // the function still uses this frame
        CHECK(done, "iteration %d: Stop returned while the function was still running", i);
    }
    return true;
}

// B1: the demuxer at EOF joins a decoder that waits for a free buffer (VideoDecoderThread), and the
// game's Stop comes meanwhile. Stop has to wake the decoder although the demuxer is already joining it.
bool StopWakesAThreadBeingJoinedByAnother() {
    for (int i = 0; i < 20; ++i) {
        Thread v;
        std::mutex mutex;
        std::condition_variable_any cv;
        std::atomic<bool> v_returned{false}, d_returned{false};
        v.Run([&](std::stop_token stop) {
            std::unique_lock lock(mutex);
            cv.wait(lock, stop, [] { return false; });
            v_returned = true;
        });
        std::thread d([&] {
            v.Join();
            d_returned = true;
        });
        SleepMs(i % 2 == 0 ? 20 : 1); // D inside Join, or just getting there
        const auto begin = Clock::now();
        v.Stop();
        const long long stop_ms = MsSince(begin);
        const bool joined = WaitFor(d_returned, 1000);
        if (joined) d.join();
        else d.detach();
        CHECK(stop_ms < 1000, "iteration %d: Stop took %lld ms", i, stop_ms);
        CHECK(joined, "iteration %d: the other joiner did not come back within 1 s of Stop", i);
        CHECK(v_returned, "iteration %d: Stop returned before the function", i);
    }
    return true;
}

// The demuxer and Stop again, without a stop request: both joiners come back, and only after the
// function has returned.
bool TwoJoinersBothWaitForTheFunction() {
    for (int i = 0; i < 20; ++i) {
        Thread t;
        std::atomic<bool> finished{false};
        std::atomic<int> after{0};
        t.Run([&](std::stop_token) {
            SleepMs(20);
            finished = true;
        });
        const auto joiner = [&] {
            t.Join();
            if (finished) ++after;
        };
        std::thread a(joiner), b(joiner);
        a.join();
        b.join();
        CHECK(after == 2, "iteration %d: %d of 2 joiners came back before the function returned", i,
              2 - after.load());
    }
    return true;
}

// AvPlayerSource restarts its threads on every Start, AvPlayerState stops its controller in the
// destructor, and "already stopped" (HasRunningThreads) relies on Joinable().
bool RestartAndDestructor() {
    {
        Thread t;
        CHECK(!t.Joinable(), "a Thread never started is joinable");
        t.Stop();
        t.Join();
    }
    {
        Thread t;
        std::atomic<int> runs{0}, stopped{0};
        const auto func = [&](std::stop_token stop) {
            ++runs;
            while (!stop.stop_requested()) SleepMs(1);
            ++stopped;
        };
        t.Run(func);
        for (int waited = 0; runs < 1 && waited < 1000; ++waited) SleepMs(1);
        CHECK(runs == 1 && t.Joinable(), "first Run: runs %d, joinable %d", runs.load(),
              int(t.Joinable()));
        t.Stop();
        CHECK(stopped == 1 && !t.Joinable(), "Stop: stopped %d, joinable %d", stopped.load(),
              int(t.Joinable()));
        t.Run(func);
        for (int waited = 0; runs < 2 && waited < 1000; ++waited) SleepMs(1);
        CHECK(runs == 2, "second Run after Stop did not run (runs %d)", runs.load());
        t.Run(func); // on a running Thread: stops the previous function first
        CHECK(stopped == 2, "Run on a running Thread returned before stopping the previous function");
        for (int waited = 0; runs < 3 && waited < 1000; ++waited) SleepMs(1);
        CHECK(runs == 3, "third Run did not run (runs %d)", runs.load());
        t.Stop();
        CHECK(stopped == 3, "last Stop: stopped %d", stopped.load());
    }
    {
        std::atomic<bool> stopped{false};
        {
            Thread t;
            t.Run([&](std::stop_token stop) {
                while (!stop.stop_requested()) SleepMs(1);
                SleepMs(20);
                stopped = true;
            });
            SleepMs(5);
        }
        CHECK(stopped, "the destructor returned before the function");
    }
    {
        Thread t;
        std::atomic<bool> joined{false};
        t.Run([&](std::stop_token) {
            t.Join();
            joined = true;
        });
        CHECK(WaitFor(joined, 1000), "a thread that joins itself did not go on");
        CHECK(!t.Joinable(), "a thread that joined itself is still joinable");
        t.Stop();
    }
    return true;
}

struct Case {
    const char* name;
    int budget_ms;
    bool (*run)();
};

const Case kCases[] = {
    {"self_join_vs_immediate_stop", 15000, SelfJoinVsImmediateStop},
    {"stop_wakes_a_thread_being_joined_by_another", 5000, StopWakesAThreadBeingJoinedByAnother},
    {"two_joiners_both_wait_for_the_function", 5000, TwoJoinersBothWaitForTheFunction},
    {"restart_and_destructor", 5000, RestartAndDestructor},
};

} // namespace

int main(int argc, char** argv) {
    const char* dir = std::getenv("TMPDIR");
    if (dir == nullptr) dir = std::getenv("TEMP");
    if (dir == nullptr) dir = "/tmp";
    g_stderr_path = std::string(dir) + "/kernel-thread-test-" + std::to_string(getpid()) + ".stderr";
    FILE* capture = std::fopen(g_stderr_path.c_str(), "ab"); // append: fd 2 writes always land at the end
    if (capture == nullptr || dup2(fileno(capture), 2) < 0) {
        std::printf("cannot redirect stderr to %s\n", g_stderr_path.c_str());
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::thread(Watchdog).detach();

    int failed = 0, ran = 0;
    for (const Case& test : kCases) {
        bool selected = argc < 2;
        for (int arg = 1; arg < argc; ++arg) selected |= std::strcmp(argv[arg], test.name) == 0;
        if (!selected) continue;
        ++ran;
        const int failures_before = JoinFailures(CapturedStderr());
        const auto begin = Clock::now();
        g_failure[0] = '\0';
        g_deadline = MsSince(g_begin) + test.budget_ms;
        g_case = test.name;
        bool ok = test.run();
        g_case = nullptr;
        const long long ms = MsSince(begin);
        const int join_failures = JoinFailures(CapturedStderr()) - failures_before;
        if (ok && join_failures > 0) ok = Fail("%d \"join failed\" lines on stderr", join_failures);
        if (ok) {
            std::printf("PASS: %s (%lld ms)\n", test.name, ms);
        } else {
            ++failed;
            std::printf("FAIL: %s: %s (%lld ms)\n", test.name, g_failure, ms);
        }
    }
    if (failed == 0) {
        std::remove(g_stderr_path.c_str());
        std::printf("kernel-thread-test: %d cases passed\n", ran);
        return 0;
    }
    std::printf("kernel-thread-test: %d of %d cases failed; stderr: %s\n", failed, ran,
                g_stderr_path.c_str());
    return 1;
}
