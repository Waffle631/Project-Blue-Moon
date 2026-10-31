// SPDX-License-Identifier: GPL-2.0-or-later
// tsuki: PNG captures of presented frames, for before/after comparisons of rendering changes
// (docs/specs/2026-10-14-hito-primer-fotograma.md, H3). No Vulkan here: the presenter copies the
// image to host memory and hands the pixels to SavePng.
//   TSUKI_SCREENSHOT_AT=120,600  frames to capture (presentation frame numbers)
//   TSUKI_SCREENSHOT_DIR=<dir>   output directory (default out/baseline, created on first capture)
// F12 asks for a capture of the next frame (RequestNow).

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace TsukiScreenshot {

enum class PixelOrder { Bgra, Rgba }; ///< byte order of each 4-byte pixel; alpha is ignored

/// Comma-separated frame numbers ("120,600"); empty text is an empty list. False (and `out`
/// empty) on anything else: signs, spaces, empty items, numbers that do not fit.
bool ParseFrameList(std::string_view text, std::set<std::uint64_t>& out);

/// Which frames to capture: a fixed list plus one-shot requests (thread-safe; the list is
/// immutable after construction).
class Schedule {
public:
    explicit Schedule(std::set<std::uint64_t> frames = {}) : frames_(std::move(frames)) {}
    /// Capture the next frame ShouldCapture sees (F12, from the event thread).
    void RequestNow() { requested_.store(true, std::memory_order_release); }
    /// True when `frame` is listed or a request is pending; consumes the request.
    bool ShouldCapture(std::uint64_t frame) {
        const bool asked = requested_.load(std::memory_order_relaxed) &&
                           requested_.exchange(false, std::memory_order_acq_rel);
        return asked || (!frames_.empty() && frames_.contains(frame));
    }
    const std::set<std::uint64_t>& Frames() const { return frames_; }

private:
    std::set<std::uint64_t> frames_;
    std::atomic<bool> requested_{false};
};

/// The process schedule, built from TSUKI_SCREENSHOT_AT on first use. A malformed list is
/// reported on stderr and captures nothing (never a guess at what was meant).
Schedule& Global();
/// TSUKI_SCREENSHOT_DIR, or out/baseline when unset or empty.
std::filesystem::path OutputDirectory();
/// frame_<NNNNNN>_<w>x<h>.png
std::string FileName(std::uint64_t frame, std::uint32_t width, std::uint32_t height);

/// Encodes `width * height` tightly packed 4-byte pixels, rows top to bottom, as an RGB PNG.
/// False (and `out` empty) on bad sizes or when FFmpeg has no PNG encoder.
bool EncodePng(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height,
               PixelOrder order, std::vector<std::uint8_t>& out);
/// Encodes and writes `dir`/FileName(...) (creating `dir`); the file appears complete or not at
/// all (written beside it, then renamed). On failure `error` says why.
bool SavePng(const std::filesystem::path& dir, std::uint64_t frame, const std::uint8_t* pixels,
             std::uint32_t width, std::uint32_t height, PixelOrder order,
             std::filesystem::path& written, std::string& error);

} // namespace TsukiScreenshot
