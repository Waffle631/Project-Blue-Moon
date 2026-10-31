// SPDX-License-Identifier: GPL-2.0-or-later
#include "tsuki_screenshot.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace Gow3Screenshot {

namespace {
constexpr std::uint32_t kMaxSide = 16384; // Vulkan's usual maxImageDimension2D
}

bool ParseFrameList(std::string_view text, std::set<std::uint64_t>& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    for (std::size_t start = 0;;) {
        const std::size_t comma = text.find(',', start);
        const std::string_view item = text.substr(start, comma == text.npos ? text.npos : comma - start);
        std::uint64_t frame = 0;
        const auto [end, ec] = std::from_chars(item.data(), item.data() + item.size(), frame);
        // from_chars accepts neither signs nor spaces for unsigned values; an empty item fails too.
        if (item.empty() || ec != std::errc{} || end != item.data() + item.size()) {
            out.clear();
            return false;
        }
        out.insert(frame);
        if (comma == text.npos) {
            return true;
        }
        start = comma + 1;
    }
}

Schedule& Global() {
    static Schedule schedule = [] {
        const char* env = std::getenv("TSUKI_SCREENSHOT_AT");
        std::set<std::uint64_t> frames;
        if (env && !ParseFrameList(env, frames)) {
            std::fprintf(stderr, "TSUKI_SCREENSHOT_AT: malformed frame list '%s' (expected e.g. "
                                 "120,600); no scheduled captures\n", env);
        }
        return Schedule(std::move(frames));
    }();
    return schedule;
}

std::filesystem::path OutputDirectory() {
    const char* env = std::getenv("TSUKI_SCREENSHOT_DIR");
    return env && *env ? std::filesystem::path(env) : std::filesystem::path("out/baseline");
}

std::string FileName(std::uint64_t frame, std::uint32_t width, std::uint32_t height) {
    char name[96];
    std::snprintf(name, sizeof(name), "frame_%06llu_%ux%u.png", static_cast<unsigned long long>(frame),
                  width, height);
    return name;
}

bool EncodePng(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height,
               PixelOrder order, std::vector<std::uint8_t>& out) {
    out.clear();
    if (!pixels || !width || !height || width > kMaxSide || height > kMaxSide) {
        return false;
    }
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    AVCodecContext* context = codec ? avcodec_alloc_context3(codec) : nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    bool ok = context && frame && packet;
    if (ok) {
        context->width = int(width);
        context->height = int(height);
        context->pix_fmt = AV_PIX_FMT_RGB24;
        context->time_base = {1, 1};
        frame->format = AV_PIX_FMT_RGB24;
        frame->width = int(width);
        frame->height = int(height);
        ok = avcodec_open2(context, codec, nullptr) == 0 && av_frame_get_buffer(frame, 0) == 0;
    }
    if (ok) {
        // Swapchain images are BGRA on most drivers, RGBA on some; the PNG has no alpha because
        // presented alpha is meaningless (and often 0, which would make the capture transparent).
        const int r = order == PixelOrder::Bgra ? 2 : 0, b = 2 - r;
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::uint8_t* src = pixels + std::size_t(y) * width * 4;
            std::uint8_t* dst = frame->data[0] + std::size_t(y) * frame->linesize[0];
            for (std::uint32_t x = 0; x < width; ++x, src += 4, dst += 3) {
                dst[0] = src[r];
                dst[1] = src[1];
                dst[2] = src[b];
            }
        }
        ok = avcodec_send_frame(context, frame) == 0 && avcodec_send_frame(context, nullptr) == 0 &&
             avcodec_receive_packet(context, packet) == 0 && packet->size > 0;
    }
    if (ok) {
        out.assign(packet->data, packet->data + packet->size);
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&context);
    return ok;
}

bool SavePng(const std::filesystem::path& dir, std::uint64_t frame, const std::uint8_t* pixels,
             std::uint32_t width, std::uint32_t height, PixelOrder order,
             std::filesystem::path& written, std::string& error) {
    written.clear();
    error.clear();
    std::vector<std::uint8_t> png;
    if (!EncodePng(pixels, width, height, order, png)) {
        error = "PNG encoding failed (" + std::to_string(width) + "x" + std::to_string(height) + ")";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = "cannot create " + dir.string() + ": " + ec.message();
        return false;
    }
    const std::filesystem::path path = dir / FileName(frame, width, height);
    std::filesystem::path partial = path;
    partial += ".part";
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        if (!file.flush()) {
            file.close();
            std::filesystem::remove(partial, ec);
            error = "cannot write " + partial.string();
            return false;
        }
    }
    std::filesystem::rename(partial, path, ec);
    if (ec) {
        error = "cannot rename to " + path.string() + ": " + ec.message();
        std::filesystem::remove(partial, ec);
        return false;
    }
    written = path;
    return true;
}

} // namespace Gow3Screenshot
