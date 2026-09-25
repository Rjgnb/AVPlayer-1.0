#pragma once

#include <memory>

#include "av/core/Status.h"
#include "av/core/Types.h"

namespace av::media {

class Frame;

// 像素格式转换（sws）。同样用 pimpl 把 FFmpeg 关在 .cpp 里。
// 视频侧只在"后端不支持源码格式"时才会被用到（SDL 直接吃 YUV420P）。
class VideoConverter
{
public:
    VideoConverter();
    ~VideoConverter();

    VideoConverter(VideoConverter&& other) noexcept;
    VideoConverter& operator=(VideoConverter&& other) noexcept;
    VideoConverter(const VideoConverter&)            = delete;
    VideoConverter& operator=(const VideoConverter&) = delete;

    core::Status Configure(const core::VideoFormat& input, core::PixelFormat outputFormat);
    void         Close();
    bool         IsConfigured() const noexcept;

    core::PixelFormat OutputFormat() const noexcept;
    core::Size        OutputSize() const noexcept;

    // 转换一帧；返回的视图指向内部缓冲（下一次 Convert() 或 Close() 前有效）。
    // input == output 时零拷贝直通。
    core::Result<core::VideoFrameView> Convert(const Frame& frame,
                                               double        ptsSeconds,
                                               double        durationSeconds = 0.0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace av::media