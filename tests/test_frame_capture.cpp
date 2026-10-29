// SPDX-License-Identifier: GPL-2.0-or-later
// The frame analyzer (vk_frame_capture.cpp), with no game or Vulkan device:
// - A binding whose bytes the host cannot read (data null: a constant-ring copy of the flattened
//   user data carries guest address 0, issue #174) is skipped instead of read through the null pointer.
// - A recorded pass lists the mip levels of what it samples and its samplers, as the guest asks for
//   them and as Vulkan gets them (Spec 6 block A, A2: is a filter ours or the game's?).
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_frame_capture.h"
#include "video_core/texture_cache/image_info.h"

using Vulkan::FrameCapture;

static VideoCore::ImageInfo Image(VAddr address, u32 width, u32 height, u32 levels) {
    VideoCore::ImageInfo info;
    info.guest_address = address;
    info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    info.size.width = width;
    info.size.height = height;
    info.resources.levels = levels;
    return info;
}

static std::string Capture(const std::filesystem::path& dir) {
    const auto display = Image(0x1000, 1920, 1080, 1);
    const auto pass = Image(0x2000, 1920, 1080, 1);
    const auto thumbnail = Image(0x3000, 512, 256, 5);
    const VideoCore::ImageInfo* display_target[] = {&display};
    const VideoCore::ImageInfo* pass_target[] = {&pass};
    FrameCapture::AddDisplayBuffer(0x1000);
    FrameCapture::Request();
    FrameCapture::OnFlip(0x1000);
    FrameCapture::BeginPass(display_target, 1, nullptr, ""); // frame boundary: recording starts
    FrameCapture::BeginPass(pass_target, 1, nullptr, "");
    AmdGpu::Sampler sampler{};
    sampler.xy_mag_filter.Assign(AmdGpu::Filter::Point);
    sampler.xy_min_filter.Assign(AmdGpu::Filter::Bilinear);
    sampler.mip_filter.Assign(AmdGpu::MipFilter::Linear);
    sampler.lod_bias.Assign(128); // 0.5
    sampler.max_lod.Assign(4 * 256);
    FrameCapture::Sampled(thumbnail, false);
    FrameCapture::SamplerUsed(sampler, vk::Filter::eNearest, vk::Filter::eLinear,
                              vk::SamplerMipmapMode::eLinear, 0.5f);
    FrameCapture::SamplerUsed(sampler, vk::Filter::eNearest, vk::Filter::eLinear,
                              vk::SamplerMipmapMode::eLinear, 0.5f); // the same one twice: listed once
    FrameCapture::Draw(1, 2, 6, 1);
    FrameCapture::BeginPass(display_target, 1, nullptr, ""); // next frame: the file is written
    for (const auto& file : std::filesystem::directory_iterator(dir)) {
        std::ifstream in(file.path());
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    return {};
}

int main() {
    // A buffer with host bytes is dumped as before.
    std::array<float, 16> words{};
    FrameCapture::Buffer(0x1234, 0, 0x1000, words.data(), sizeof(words));
    // Flattened user data in the constant ring: host copy only, guest address 0, and here
    // nothing readable at all. Must not dereference `data`.
    FrameCapture::Buffer(0x1234, 1, 0, nullptr, 64);

    const auto dir = std::filesystem::temp_directory_path() / "gow3-frame-capture-test";
    std::filesystem::remove_all(dir);
    setenv("GOW3_CAPTURE_DIR", dir.c_str(), 1);
    const std::string text = Capture(dir);
    std::filesystem::remove_all(dir);
    std::fputs(text.c_str(), stdout);
    assert(text.find("  samples 0x3000 R8G8B8A8Unorm 512x256 mips 5\n") != std::string::npos);
    const std::string sampler = "  sampler guest mag Point min Bilinear mip Linear aniso One lod bias 0.5 "
                                "range 0-4 -> vk mag Nearest min Linear mipmap Linear lod bias 0.5\n";
    const auto at = text.find(sampler);
    assert(at != std::string::npos && text.find(sampler, at + 1) == std::string::npos);
    // Render targets keep their line: mips are only said for what a pass samples.
    assert(text.find("  color0 0x2000 R8G8B8A8Unorm 1920x1080\n") != std::string::npos);
    std::puts("frame-capture-test ok");
    return 0;
}
