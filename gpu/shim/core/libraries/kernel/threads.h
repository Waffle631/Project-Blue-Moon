// gow3: host threads that may call guest code (AvPlayer allocator callbacks).
// Each thread gets a guest TCB (GS base, TLS) from the C runtime before running.
#pragma once
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <system_error>
#include <thread>
#include "common/types.h"

extern "C" void runtime_thread_attach_host(const char* name);

namespace Libraries::Kernel {
// gow3: AvPlayer joins one thread from several threads at once: a decoder from the demuxer at EOF
// and from the game's sceAvPlayerStop, and every thread joins itself on its way out
// (avplayer_source.cpp). One std::jthread shared by those calls raced: the second pthread_join of a
// thread returned EINVAL ("join failed (Invalid argument)", out/run-audiodec-a1.log) or never
// returned, which hung sceAvPlayerStop at the end of phasetitle_01_00_en.mp4 (docs/avplayer.md §3,
// tests/test_kernel_thread.cpp). Now exactly one caller takes the std::thread out, under a mutex,
// and joins it; everybody else waits for the function to return.
class Thread {
public:
    Thread() = default;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    ~Thread() { Stop(); }

    void Run(std::function<void(std::stop_token)>&& func) {
        Stop();
        auto s = std::make_shared<State>();
        // The new thread may join itself at once: it waits for this lock, so it sees `state` set
        // (set only once the thread exists, or Join would wait for a function that never runs).
        std::lock_guard lock(s->mutex);
        s->thread = std::thread([s, func = std::move(func)]() mutable {
            {
                std::lock_guard lock(s->mutex);
                s->id = std::this_thread::get_id();
            }
            runtime_thread_attach_host("gow3:hle");
            func(s->stop.get_token());
            func = nullptr; // what the function owns goes before anyone sees it finished
            std::lock_guard lock(s->mutex);
            s->id = {};
            s->done = true;
            s->cv.notify_all();
        });
        state = s;
    }
    // Returns once the function has returned, except on the thread itself, which lets go of its own
    // std::thread and goes on (the demuxer and the decoders end that way).
    void Join() { Join(state); }
    // Whether nobody has taken the std::thread out yet: AvPlayerSource::HasRunningThreads and its
    // "already stopped" rely on it.
    bool Joinable() const {
        const auto s = state;
        if (s == nullptr) return false;
        std::lock_guard lock(s->mutex);
        return s->thread.joinable();
    }
    // Always asks the function to stop, even when another thread is already joining it: a decoder
    // the demuxer is joining may be waiting for a buffer that only the stop request ends.
    void Stop() {
        const auto s = state;
        if (s == nullptr) return;
        s->stop.request_stop();
        Join(s);
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        std::stop_source stop;
        std::thread thread;
        std::thread::id id; // of the running function; cleared when it returns (ids are reused)
    };

    static void Join(const std::shared_ptr<State>& s) {
        if (s == nullptr) return;
        std::unique_lock lock(s->mutex);
        if (s->id == std::this_thread::get_id()) {
            std::thread self = std::move(s->thread);
            lock.unlock();
            if (self.joinable()) self.detach();
            return;
        }
        if (s->thread.joinable()) {
            std::thread thread = std::move(s->thread);
            lock.unlock();
            // gow3: called from guest code, which a C++ exception cannot unwind. Windows (libc++ on
            // Win32 threads) can fail the join although the thread is fine: log it, let go of the
            // handle and wait for the function below like any other joiner.
            try {
                thread.join();
            } catch (const std::system_error& error) {
                std::fprintf(stderr, "HLE thread: join failed (%s); waiting for it to finish\n",
                             error.what());
                if (thread.joinable()) thread.detach();
            }
            lock.lock();
        }
        s->cv.wait(lock, [&] { return s->done; });
    }

    std::shared_ptr<State> state;
};
} // namespace Libraries::Kernel
