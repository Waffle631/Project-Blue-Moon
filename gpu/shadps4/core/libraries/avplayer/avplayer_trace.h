// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: GOW3_TRACE_AVPLAYER=1 writes one stderr line per libSceAvPlayer call the game makes and per
// event AvPlayer sends it, to learn the game's real contract (Spec 5, paso 1; docs/avplayer.md).
// Lines read "AvTrace +<ms> t<tid>: ...", <ms> counted from the first traced call, whose wall clock
// time the first line gives. With the variable unset a call site costs one load of a static bool.

#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#ifndef _WIN32
#include <unistd.h> // gettid; Windows gets it from the force-included gow3_win_compat.h
#endif

namespace Libraries::AvPlayer {

inline bool AvTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("GOW3_TRACE_AVPLAYER");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

// Milliseconds since the first traced call: the wall clock the frame timestamps are measured against.
inline long long AvTraceMs() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 start)
        .count();
}

[[gnu::format(printf, 1, 2)]] inline void AvTrace(const char* fmt, ...) {
    if (!AvTraceEnabled()) {
        return;
    }
    static const bool announced = [] {
        AvTraceMs();
        const std::time_t now = std::time(nullptr);
        char clock[16] = "?";
        std::strftime(clock, sizeof(clock), "%H:%M:%S", std::localtime(&now));
        std::fprintf(stderr, "AvTrace: GOW3_TRACE_AVPLAYER on, +0 is %s\n", clock);
        return true;
    }();
    (void)announced;
    char text[480];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    // One write per line, so lines from the game, controller and decoder threads do not interleave.
    char line[560];
    int length = std::snprintf(line, sizeof(line), "AvTrace +%lld t%d: %s\n", AvTraceMs(),
                               static_cast<int>(gettid()), text);
    if (length < 0) {
        return;
    }
    if (length >= int(sizeof(line))) {
        length = int(sizeof(line)) - 1;
        line[length - 1] = '\n';
    }
    std::fwrite(line, 1, size_t(length), stderr);
}

} // namespace Libraries::AvPlayer
