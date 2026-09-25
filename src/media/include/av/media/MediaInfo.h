#pragma once

#include "av/media/FFmpegCompat.h"

#include <string>

#include "av/core/Rational.h"
#include "av/core/Types.h"

namespace av::media {

struct StreamInfo
{
    AVStream*      stream   = nullptr;   // 由 Demuxer 持有，生命周期 ≤ AVFormatContext
    int            index    = -1;
    core::Rational timeBase{ 1, 1 };
    double         durationSeconds = 0.0;
    std::int64_t   bitRate = 0;
};

struct AudioStreamInfo : StreamInfo
{
    core::AudioFormat format;     // 解码器输出格式（音频帧的原始格式）
};

struct VideoStreamInfo : StreamInfo
{
    core::VideoFormat format;     // 解码器输出格式（通常是 Yuv420P）
    double            frameRate = 0.0;
};

struct MediaInfo
{
    std::string url;
    std::string containerFormat;
    std::string codecSummary;        // 形如 "h264 + aac"
    double      durationSeconds = 0.0;   // 0 表示未知（直播流）
    std::int64_t bitRate = 0;
    std::size_t  streamCount = 0;

    bool hasAudio = false;
    bool hasVideo = false;
    AudioStreamInfo audio;
    VideoStreamInfo video;

    bool HasDuration() const noexcept { return durationSeconds > 0.0; }
};

} // namespace av::media
