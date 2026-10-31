// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/libraries/avplayer/avplayer.h"
#include "core/libraries/avplayer/avplayer_error.h"
#include "core/libraries/avplayer/avplayer_impl.h"
#include "core/libraries/avplayer/avplayer_trace.h"
#include "core/libraries/libs.h"

#include <mutex>
#include <string_view>
#include <unordered_map>

namespace Libraries::AvPlayer {

namespace {

// tsuki: TSUKI_TRACE_AVPLAYER (avplayer_trace.h). The game polls GetVideoData(Ex), GetAudioData,
// IsActive and CurrentTime every frame, so those are counted per player and only their useful
// calls are traced: frames delivered, IsActive changes, CurrentTime once a second. Close prints the
// totals.
struct TraceStats {
    long long start_ms = -1; // AvTraceMs() of the last sceAvPlayerStart
    unsigned long long video_true = 0, video_false = 0, last_video_ts = 0;
    unsigned long long audio_true = 0, audio_false = 0, last_audio_ts = 0;
    unsigned long long active_calls = 0, time_calls = 0;
    int last_active = -1;
    long long last_time_trace_ms = -1;
};

std::mutex g_trace_mutex;
std::unordered_map<AvPlayerHandle, TraceStats> g_trace_stats;

// Milliseconds since this player's Start, -1 before it: the wall time a frame timestamp is
// compared with. Call with g_trace_mutex held.
long long PlayMs(const TraceStats& stats) {
    return stats.start_ms < 0 ? -1 : AvTraceMs() - stats.start_ms;
}

s32 Invalid(const char* call, AvPlayerHandle handle) {
    AvTrace("%s h=%p -> invalid params", call, handle);
    return ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
}

const char* Str(const char* text) {
    return text != nullptr ? text : "(null)";
}

void TraceVideoFrame(const char* call, AvPlayerHandle handle, bool delivered, u64 timestamp,
                     u32 width, u32 height, u32 pitch) {
    std::lock_guard lock(g_trace_mutex);
    auto& stats = g_trace_stats[handle];
    if (!delivered) {
        ++stats.video_false;
        return;
    }
    ++stats.video_true;
    stats.last_video_ts = timestamp;
    AvTrace("%s h=%p frame ts=%llu %ux%u pitch=%u play=%lld", call, handle,
            (unsigned long long)timestamp, width, height, pitch, PlayMs(stats));
}

} // namespace

s32 PS4_SYSV_ABI sceAvPlayerAddSource(AvPlayerHandle handle, const char* filename) {
    LOG_TRACE(Lib_AvPlayer, "filename = {}", filename);
    if (handle == nullptr) {
        return Invalid("AddSource", handle);
    }
    const s32 result = handle->AddSource(filename);
    AvTrace("AddSource h=%p path=\"%s\" -> %#x", handle, Str(filename), result);
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerAddSourceEx(AvPlayerHandle handle, AvPlayerUriType uri_type,
                                        AvPlayerSourceDetails* source_details) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr || uri_type != AvPlayerUriType::Source || source_details == nullptr) {
        AvTrace("AddSourceEx h=%p uri_type=%u details=%p -> invalid params", handle,
                u32(uri_type), source_details);
        return ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
    }
    const auto path = std::string_view(source_details->uri.name, source_details->uri.length);
    const s32 result = handle->AddSourceEx(path, source_details->source_type);
    AvTrace("AddSourceEx h=%p path=\"%.*s\" source_type=%d -> %#x", handle, int(path.size()),
            path.data(), int(source_details->source_type), result);
    return result;
}

int PS4_SYSV_ABI sceAvPlayerChangeStream() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    AvTrace("ChangeStream (stub) -> 0");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerClose(AvPlayerHandle handle) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        return Invalid("Close", handle);
    }
    if (AvTraceEnabled()) {
        std::lock_guard lock(g_trace_mutex);
        const auto stats = g_trace_stats[handle];
        g_trace_stats.erase(handle);
        AvTrace("Close h=%p begin; video %llu frames (last ts %llu) + %llu empty calls, audio "
                "%llu blocks (last ts %llu) + %llu empty calls, IsActive %llu calls, CurrentTime "
                "%llu calls",
                handle, stats.video_true, stats.last_video_ts, stats.video_false,
                stats.audio_true, stats.last_audio_ts, stats.audio_false, stats.active_calls,
                stats.time_calls);
    }
    const long long begin = AvTraceEnabled() ? AvTraceMs() : 0;
    delete handle;
    AvTrace("Close h=%p -> 0 (%lld ms)", handle, AvTraceEnabled() ? AvTraceMs() - begin : 0);
    return ORBIS_OK;
}

u64 PS4_SYSV_ABI sceAvPlayerCurrentTime(AvPlayerHandle handle) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        return Invalid("CurrentTime", handle);
    }
    const u64 result = handle->CurrentTime();
    if (AvTraceEnabled()) {
        std::lock_guard lock(g_trace_mutex);
        auto& stats = g_trace_stats[handle];
        ++stats.time_calls;
        const long long now = AvTraceMs();
        if (stats.last_time_trace_ms < 0 || now - stats.last_time_trace_ms >= 1000) {
            stats.last_time_trace_ms = now;
            AvTrace("CurrentTime h=%p -> %llu play=%lld (call %llu)", handle,
                    (unsigned long long)result, PlayMs(stats), stats.time_calls);
        }
    }
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerDisableStream(AvPlayerHandle handle, u32 stream_id) {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    if (handle == nullptr) {
        return Invalid("DisableStream", handle);
    }
    AvTrace("DisableStream h=%p stream=%u (stub) -> 0", handle, stream_id);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerEnableStream(AvPlayerHandle handle, u32 stream_id) {
    LOG_TRACE(Lib_AvPlayer, "stream_id = {}", stream_id);
    if (handle == nullptr) {
        return Invalid("EnableStream", handle);
    }
    const s32 result = handle->EnableStream(stream_id);
    AvTrace("EnableStream h=%p stream=%u -> %#x", handle, stream_id, result);
    return result;
}

bool PS4_SYSV_ABI sceAvPlayerGetAudioData(AvPlayerHandle handle, AvPlayerFrameInfo* p_info) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr || p_info == nullptr) {
        AvTrace("GetAudioData h=%p info=%p -> false (invalid params)", handle, p_info);
        return false;
    }
    const bool result = handle->GetAudioData(*p_info);
    if (AvTraceEnabled()) {
        std::lock_guard lock(g_trace_mutex);
        auto& stats = g_trace_stats[handle];
        if (!result) {
            ++stats.audio_false;
        } else {
            ++stats.audio_true;
            stats.last_audio_ts = p_info->timestamp;
            AvTrace("GetAudioData h=%p block ts=%llu ch=%u rate=%u size=%u play=%lld", handle,
                    (unsigned long long)p_info->timestamp, p_info->details.audio.channel_count,
                    p_info->details.audio.sample_rate, p_info->details.audio.size, PlayMs(stats));
        }
    }
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerGetStreamInfo(AvPlayerHandle handle, u32 stream_id,
                                          AvPlayerStreamInfo* p_info) {
    LOG_TRACE(Lib_AvPlayer, "stream_id = {}", stream_id);
    if (handle == nullptr || p_info == nullptr) {
        return Invalid("GetStreamInfo", handle);
    }
    const s32 result = handle->GetStreamInfo(stream_id, *p_info);
    if (AvTraceEnabled()) {
        const auto& info = *p_info;
        const auto duration = (unsigned long long)info.duration;
        const auto start = (unsigned long long)info.start_time;
        if (result != ORBIS_OK) {
            AvTrace("GetStreamInfo h=%p stream=%u -> %#x", handle, stream_id, result);
        } else if (info.type == AvPlayerStreamType::Video) {
            AvTrace("GetStreamInfo h=%p stream=%u -> 0 video %ux%u aspect=%.3f lang=%.4s "
                    "duration=%llu start=%llu",
                    handle, stream_id, info.details.video.width, info.details.video.height,
                    info.details.video.aspect_ratio, info.details.video.language_code, duration,
                    start);
        } else if (info.type == AvPlayerStreamType::Audio) {
            AvTrace("GetStreamInfo h=%p stream=%u -> 0 audio ch=%u rate=%u size=%u lang=%.4s "
                    "duration=%llu start=%llu",
                    handle, stream_id, info.details.audio.channel_count,
                    info.details.audio.sample_rate, info.details.audio.size,
                    info.details.audio.language_code, duration, start);
        } else {
            AvTrace("GetStreamInfo h=%p stream=%u -> 0 type=%u duration=%llu start=%llu", handle,
                    stream_id, u32(info.type), duration, start);
        }
    }
    return result;
}

bool PS4_SYSV_ABI sceAvPlayerGetVideoData(AvPlayerHandle handle, AvPlayerFrameInfo* video_info) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr || video_info == nullptr) {
        AvTrace("GetVideoData h=%p info=%p -> false (invalid params)", handle, video_info);
        return false;
    }
    const bool result = handle->GetVideoData(*video_info);
    if (AvTraceEnabled()) {
        TraceVideoFrame("GetVideoData", handle, result, video_info->timestamp,
                        video_info->details.video.width, video_info->details.video.height, 0);
    }
    return result;
}

bool PS4_SYSV_ABI sceAvPlayerGetVideoDataEx(AvPlayerHandle handle,
                                            AvPlayerFrameInfoEx* video_info) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr || video_info == nullptr) {
        AvTrace("GetVideoDataEx h=%p info=%p -> false (invalid params)", handle, video_info);
        return false;
    }
    const bool result = handle->GetVideoData(*video_info);
    if (AvTraceEnabled()) {
        TraceVideoFrame("GetVideoDataEx", handle, result, video_info->timestamp,
                        video_info->details.video.width, video_info->details.video.height,
                        video_info->details.video.pitch);
    }
    return result;
}

AvPlayerHandle PS4_SYSV_ABI sceAvPlayerInit(AvPlayerInitData* data) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (data == nullptr) {
        AvTrace("Init data=null -> null");
        return nullptr;
    }

    if (data->memory_replacement.allocate == nullptr ||
        data->memory_replacement.allocate_texture == nullptr ||
        data->memory_replacement.deallocate == nullptr ||
        data->memory_replacement.deallocate_texture == nullptr) {
        LOG_ERROR(Lib_AvPlayer, "All allocators are required for AvPlayer Initialisation.");
        AvTrace("Init -> null (an allocator is missing)");
        return nullptr;
    }

    const auto handle = new AvPlayer(*data);
    AvTrace("Init -> h=%p file_replacement=%s events=%s game_obj=%p framebuffers=%d "
            "auto_start=%d debug=%d base_priority=%u lang=\"%.4s\"",
            handle, data->file_replacement.open != nullptr ? "yes" : "no (path)",
            data->event_replacement.event_callback != nullptr ? "yes" : "no",
            data->event_replacement.object_ptr, data->num_output_video_framebuffers,
            int(data->auto_start), int(data->debug_level), data->base_priority,
            Str(data->default_language));
    return handle;
}

s32 PS4_SYSV_ABI sceAvPlayerInitEx(const AvPlayerInitDataEx* p_data, AvPlayerHandle* p_player) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (p_data == nullptr || p_player == nullptr) {
        AvTrace("InitEx data=%p player=%p -> invalid params", p_data, p_player);
        return ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
    }

    if (p_data->memory_replacement.allocate == nullptr ||
        p_data->memory_replacement.allocate_texture == nullptr ||
        p_data->memory_replacement.deallocate == nullptr ||
        p_data->memory_replacement.deallocate_texture == nullptr) {
        LOG_ERROR(Lib_AvPlayer, "All allocators are required for AvPlayer Initialisation.");
        AvTrace("InitEx -> invalid params (an allocator is missing)");
        return ORBIS_AVPLAYER_ERROR_INVALID_PARAMS;
    }

    AvPlayerInitData data = {};
    data.memory_replacement = p_data->memory_replacement;
    data.file_replacement = p_data->file_replacement;
    data.event_replacement = p_data->event_replacement;
    data.default_language = p_data->default_language;
    data.num_output_video_framebuffers = p_data->num_output_video_framebuffers;
    data.auto_start = p_data->auto_start;

    *p_player = new AvPlayer(data);
    // this_size against shadPS4's layout: a game built with a newer SDK could pass a larger struct.
    AvTrace("InitEx this_size=%zu (shadPS4 %zu) -> 0 h=%p file_replacement=%s events=%s "
            "game_obj=%p framebuffers=%d auto_start=%d debug=%d lang=\"%.4s\" priority/affinity "
            "audio=%u/%#x video=%u/%#x demux=%u/%#x controller=%u/%#x file=%u/%#x",
            p_data->this_size, sizeof(AvPlayerInitDataEx), *p_player,
            p_data->file_replacement.open != nullptr ? "yes" : "no (path)",
            p_data->event_replacement.event_callback != nullptr ? "yes" : "no",
            p_data->event_replacement.object_ptr, p_data->num_output_video_framebuffers,
            int(p_data->auto_start), int(p_data->debug_level), Str(p_data->default_language),
            p_data->audio_decoder_priority, p_data->audio_decoder_affinity,
            p_data->video_decoder_priority, p_data->video_decoder_affinity,
            p_data->demuxer_priority, p_data->demuxer_affinity, p_data->controller_priority,
            p_data->controller_affinity, p_data->file_streaming_priority,
            p_data->file_streaming_affinity);
    return ORBIS_OK;
}

bool PS4_SYSV_ABI sceAvPlayerIsActive(AvPlayerHandle handle) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        AvTrace("IsActive h=null -> false");
        return false;
    }
    const bool result = handle->IsActive();
    if (AvTraceEnabled()) {
        std::lock_guard lock(g_trace_mutex);
        auto& stats = g_trace_stats[handle];
        ++stats.active_calls;
        if (stats.last_active != int(result)) {
            stats.last_active = int(result);
            AvTrace("IsActive h=%p -> %s play=%lld (call %llu)", handle,
                    result ? "true" : "false", PlayMs(stats), stats.active_calls);
        }
    }
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerJumpToTime(AvPlayerHandle handle, uint64_t time) {
    LOG_TRACE(Lib_AvPlayer, "called, time (msec) = {}", time);
    if (handle == nullptr) {
        return Invalid("JumpToTime", handle);
    }
    // tsuki: no longer a stub that returned OK without moving (Spec 5, task 4;
    // AvPlayerSource::JumpToTime). It stops and restarts the decoding threads like Stop, so it
    // guards against exceptions the same way.
    const long long begin = AvTraceEnabled() ? AvTraceMs() : 0;
    s32 result;
    try {
        result = handle->JumpToTime(time);
    } catch (const std::exception& error) {
        LOG_ERROR(Lib_AvPlayer, "JumpToTime failed: {}", error.what());
        result = ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
    }
    AvTrace("JumpToTime h=%p time=%llu -> %#x (%lld ms)", handle, (unsigned long long)time, result,
            AvTraceEnabled() ? AvTraceMs() - begin : 0);
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerPause(AvPlayerHandle handle) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        return Invalid("Pause", handle);
    }
    const s32 result = handle->Pause();
    AvTrace("Pause h=%p -> %#x", handle, result);
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerPostInit(AvPlayerHandle handle, AvPlayerPostInitData* data) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr || data == nullptr) {
        return Invalid("PostInit", handle);
    }
    const s32 result = handle->PostInit(*data);
    AvTrace("PostInit h=%p demux_video_buffer=%u video_decoder=%u audio_decoder=%u -> %#x",
            handle, data->demux_video_buffer_size,
            u32(data->video_decoder_init.decoder_type.video_type),
            u32(data->audio_decoder_init.decoder_type.audio_type), result);
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerPrintf(const char* format, ...) {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerResume(AvPlayerHandle handle) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        return Invalid("Resume", handle);
    }
    const s32 result = handle->Resume();
    AvTrace("Resume h=%p -> %#x", handle, result);
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerSetAvSyncMode(AvPlayerHandle handle, AvPlayerAvSyncMode sync_mode) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        return Invalid("SetAvSyncMode", handle);
    }
    const s32 result = handle->SetAvSyncMode(sync_mode);
    AvTrace("SetAvSyncMode h=%p mode=%d -> %#x", handle, int(sync_mode), result);
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerSetLogCallback(AvPlayerLogCallback log_cb, void* user_data) {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    AvTrace("SetLogCallback cb=%p (stub) -> 0", reinterpret_cast<void*>(log_cb));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerSetLooping(AvPlayerHandle handle, bool loop_flag) {
    LOG_TRACE(Lib_AvPlayer, "called, looping = {}", loop_flag);
    if (handle == nullptr) {
        return Invalid("SetLooping", handle);
    }
    if (!handle->SetLooping(loop_flag)) {
        AvTrace("SetLooping h=%p loop=%d -> %#x", handle, int(loop_flag),
                ORBIS_AVPLAYER_ERROR_OPERATION_FAILED);
        return ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
    }
    AvTrace("SetLooping h=%p loop=%d -> 0", handle, int(loop_flag));
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerSetTrickSpeed(AvPlayerHandle handle, s32 trick_speed) {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called speed = {}", trick_speed);
    if (handle == nullptr) {
        return Invalid("SetTrickSpeed", handle);
    }
    AvTrace("SetTrickSpeed h=%p speed=%d (stub) -> 0", handle, trick_speed);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerStart(AvPlayerHandle handle) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        return Invalid("Start", handle);
    }
    const s32 result = handle->Start();
    if (AvTraceEnabled()) {
        std::lock_guard lock(g_trace_mutex);
        g_trace_stats[handle].start_ms = AvTraceMs();
        AvTrace("Start h=%p -> %#x", handle, result);
    }
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerStop(AvPlayerHandle handle) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        return Invalid("Stop", handle);
    }
    // tsuki: a begin line, because Stop is where the game hung (Spec 5, §2).
    AvTrace("Stop h=%p begin", handle);
    const long long begin = AvTraceEnabled() ? AvTraceMs() : 0;
    s32 result;
    // tsuki: guest code calls this and cannot unwind a C++ exception.
    try {
        result = handle->Stop();
    } catch (const std::exception& error) {
        LOG_ERROR(Lib_AvPlayer, "Stop failed: {}", error.what());
        result = ORBIS_AVPLAYER_ERROR_OPERATION_FAILED;
    }
    AvTrace("Stop h=%p -> %#x (%lld ms)", handle, result,
            AvTraceEnabled() ? AvTraceMs() - begin : 0);
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerStreamCount(AvPlayerHandle handle) {
    LOG_TRACE(Lib_AvPlayer, "called");
    if (handle == nullptr) {
        return Invalid("StreamCount", handle);
    }
    const s32 result = handle->GetStreamCount();
    AvTrace("StreamCount h=%p -> %d", handle, result);
    return result;
}

s32 PS4_SYSV_ABI sceAvPlayerVprintf(const char* format, va_list args) {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    AvTrace("Vprintf format=\"%.120s\" (stub) -> 0", Str(format));
    return ORBIS_OK;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("KMcEa+rHsIo", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerAddSource);
    LIB_FUNCTION("x8uvuFOPZhU", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerAddSourceEx);
    LIB_FUNCTION("buMCiJftcfw", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerChangeStream);
    LIB_FUNCTION("NkJwDzKmIlw", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerClose);
    LIB_FUNCTION("wwM99gjFf1Y", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerCurrentTime);
    LIB_FUNCTION("BOVKAzRmuTQ", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerDisableStream);
    LIB_FUNCTION("ODJK2sn9w4A", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerEnableStream);
    LIB_FUNCTION("Wnp1OVcrZgk", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerGetAudioData);
    LIB_FUNCTION("d8FcbzfAdQw", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerGetStreamInfo);
    LIB_FUNCTION("o3+RWnHViSg", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerGetVideoData);
    LIB_FUNCTION("JdksQu8pNdQ", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerGetVideoDataEx);
    LIB_FUNCTION("aS66RI0gGgo", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerInit);
    LIB_FUNCTION("o9eWRkSL+M4", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerInitEx);
    LIB_FUNCTION("UbQoYawOsfY", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerIsActive);
    LIB_FUNCTION("XC9wM+xULz8", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerJumpToTime);
    LIB_FUNCTION("9y5v+fGN4Wk", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerPause);
    LIB_FUNCTION("HD1YKVU26-M", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerPostInit);
    // LIB_FUNCTION("agig-iDRrTE", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerPrintf);
    LIB_FUNCTION("w5moABNwnRY", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerResume);
    LIB_FUNCTION("k-q+xOxdc3E", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerSetAvSyncMode);
    LIB_FUNCTION("eBTreZ84JFY", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerSetLogCallback);
    LIB_FUNCTION("OVths0xGfho", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerSetLooping);
    LIB_FUNCTION("av8Z++94rs0", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerSetTrickSpeed);
    LIB_FUNCTION("ET4Gr-Uu07s", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerStart);
    LIB_FUNCTION("ZC17w3vB5Lo", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerStop);
    LIB_FUNCTION("hdTyRzCXQeQ", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerStreamCount);
    LIB_FUNCTION("yN7Jhuv8g24", "libSceAvPlayer", 1, "libSceAvPlayer", sceAvPlayerVprintf);
};

} // namespace Libraries::AvPlayer
