// SPDX-License-Identifier: GPL-2.0-or-later
// Frame captures: frame lists, one-shot requests, file names, and PNGs that decode back to the
// same pixels in both swapchain byte orders.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

#include "../gpu/shim/tsuki_png.h"
#include "../gpu/shim/tsuki_screenshot.h"

using namespace TsukiScreenshot;

static void frame_lists() {
    std::set<std::uint64_t> frames;
    assert(ParseFrameList("120,600", frames) && frames == std::set<std::uint64_t>({120, 600}));
    assert(ParseFrameList("600,120,600", frames) && frames.size() == 2);
    assert(ParseFrameList("0", frames) && frames == std::set<std::uint64_t>({0}));
    assert(ParseFrameList("", frames) && frames.empty());
    for (const char* bad : {",", "1,", ",1", "1,,2", "-1", "+1", " 1", "1 ", "12a", "0x10",
                            "18446744073709551616"}) {
        frames = {7};
        assert(!ParseFrameList(bad, frames) && frames.empty());
    }
    assert(ParseFrameList("18446744073709551615", frames) && *frames.begin() == UINT64_MAX);
}

static void schedule() {
    Schedule listed({3, 5});
    assert(!listed.ShouldCapture(1) && !listed.ShouldCapture(2) && listed.ShouldCapture(3));
    assert(!listed.ShouldCapture(4) && listed.ShouldCapture(5) && !listed.ShouldCapture(6));
    // A request captures the next frame seen, once; on a listed frame it does not add a second one.
    listed.RequestNow();
    assert(listed.ShouldCapture(10) && !listed.ShouldCapture(11));
    listed.RequestNow();
    assert(listed.ShouldCapture(3) && !listed.ShouldCapture(4));
    Schedule none;
    assert(!none.ShouldCapture(0) && !none.ShouldCapture(120));
    none.RequestNow();
    none.RequestNow(); // two presses before a frame are one capture
    assert(none.ShouldCapture(42) && !none.ShouldCapture(43));
}

static void environment() {
    setenv("TSUKI_SCREENSHOT_AT", "2,4", 1);
    Schedule& global = Global();
    assert(&global == &Global() && global.Frames() == std::set<std::uint64_t>({2, 4}));
    setenv("TSUKI_SCREENSHOT_AT", "9", 1); // read once: later changes do not apply
    assert(!Global().ShouldCapture(9) && Global().ShouldCapture(4));

    unsetenv("TSUKI_SCREENSHOT_DIR");
    assert(OutputDirectory() == std::filesystem::path("out/baseline"));
    setenv("TSUKI_SCREENSHOT_DIR", "", 1);
    assert(OutputDirectory() == std::filesystem::path("out/baseline"));
    setenv("TSUKI_SCREENSHOT_DIR", "captures/a", 1);
    assert(OutputDirectory() == std::filesystem::path("captures/a"));

    assert(FileName(120, 1920, 1080) == "frame_000120_1920x1080.png");
    assert(FileName(1234567, 7, 5) == "frame_1234567_7x5.png");
}

// Odd sizes so a row stride mistake shows; alpha varies and must not reach the PNG.
constexpr std::uint32_t kWidth = 7, kHeight = 5;
static std::uint8_t Red(std::uint32_t x, std::uint32_t y) { return std::uint8_t(x * 36 + y); }
static std::uint8_t Green(std::uint32_t x, std::uint32_t y) { return std::uint8_t(y * 60 + x); }
static std::uint8_t Blue(std::uint32_t x, std::uint32_t y) { return std::uint8_t(255 - (x + y) * 20); }

static std::vector<std::uint8_t> Gradient(PixelOrder order) {
    std::vector<std::uint8_t> pixels(kWidth * kHeight * 4);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            std::uint8_t* p = &pixels[(y * kWidth + x) * 4];
            p[0] = order == PixelOrder::Bgra ? Blue(x, y) : Red(x, y);
            p[1] = Green(x, y);
            p[2] = order == PixelOrder::Bgra ? Red(x, y) : Blue(x, y);
            p[3] = std::uint8_t(x * 40);
        }
    }
    return pixels;
}

static void CheckGradient(const TsukiPng::Image& image) {
    assert(image.width == int(kWidth) && image.height == int(kHeight));
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const unsigned char* p = &image.rgba[(y * kWidth + x) * 4];
            assert(p[0] == Red(x, y) && p[1] == Green(x, y) && p[2] == Blue(x, y) && p[3] == 255);
        }
    }
}

static void encoding() {
    for (PixelOrder order : {PixelOrder::Bgra, PixelOrder::Rgba}) {
        const std::vector<std::uint8_t> pixels = Gradient(order);
        std::vector<std::uint8_t> png;
        assert(EncodePng(pixels.data(), kWidth, kHeight, order, png) && png.size() > 8);
        TsukiPng::Image image;
        assert(TsukiPng::DecodeRgba(png.data(), png.size(), image));
        CheckGradient(image);
    }
    // The order matters: BGRA bytes encoded as RGBA swap red and blue.
    {
        const std::vector<std::uint8_t> pixels = Gradient(PixelOrder::Bgra);
        std::vector<std::uint8_t> png;
        TsukiPng::Image image;
        assert(EncodePng(pixels.data(), kWidth, kHeight, PixelOrder::Rgba, png) &&
               TsukiPng::DecodeRgba(png.data(), png.size(), image));
        assert(image.rgba[0] == Blue(0, 0) && image.rgba[2] == Red(0, 0) && Red(0, 0) != Blue(0, 0));
    }
    std::vector<std::uint8_t> png = {1};
    const std::uint8_t pixel[4] = {};
    assert(!EncodePng(nullptr, 1, 1, PixelOrder::Rgba, png) && png.empty());
    assert(!EncodePng(pixel, 0, 1, PixelOrder::Rgba, png) && !EncodePng(pixel, 1, 0, PixelOrder::Rgba, png));
    assert(!EncodePng(pixel, 20000, 1, PixelOrder::Rgba, png) && png.empty());
}

static void saving() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("tsuki-screenshot-test-" + std::to_string(getpid()));
    std::filesystem::remove_all(root);
    const std::filesystem::path dir = root / "nested" / "baseline"; // created on demand
    const std::vector<std::uint8_t> pixels = Gradient(PixelOrder::Bgra);
    std::filesystem::path written;
    std::string error;
    assert(SavePng(dir, 120, pixels.data(), kWidth, kHeight, PixelOrder::Bgra, written, error));
    assert(error.empty() && written == dir / "frame_000120_7x5.png");
    TsukiPng::Image image;
    assert(TsukiPng::LoadRgba(written.string().c_str(), image));
    CheckGradient(image);
    // Only the finished file is left behind.
    std::size_t entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        assert(entry.path() == written);
        ++entries;
    }
    assert(entries == 1);

    // Errors are reported, nothing is written.
    assert(!SavePng(dir, 121, nullptr, kWidth, kHeight, PixelOrder::Bgra, written, error));
    assert(written.empty() && !error.empty() && !std::filesystem::exists(dir / "frame_000121_7x5.png"));
    const std::filesystem::path blocked = root / "file";
    std::FILE* file = std::fopen(blocked.string().c_str(), "wb");
    assert(file);
    std::fclose(file);
    assert(!SavePng(blocked / "sub", 1, pixels.data(), kWidth, kHeight, PixelOrder::Bgra, written, error));
    assert(written.empty() && error.find("cannot create") == 0);
    std::filesystem::remove_all(root);
}

int main() {
    frame_lists();
    schedule();
    environment();
    encoding();
    saving();
    std::puts("screenshot-test: ok");
    return 0;
}
