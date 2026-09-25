#pragma once

#include <functional>
#include <string>

#include "av/core/Status.h"
#include "av/media/MediaInfo.h"
#include "av/media/Packet.h"

namespace av::media {

struct DemuxOptions
{
    bool enableAudio = true;
    bool enableVideo = true;
    std::string formatHint;                  // 可选：强制指定 demuxer（如 "mov"）
    std::size_t maxAnalyzeBytes = 0;         // 0 = 用 FFmpeg 默认
    bool        allowNetwork = false;        // 默认只允许本地文件/设备
    bool        preferLowLatency = false;
};

// 解封装层：把"容器"变成一个"包流"。
// 线程约定：**只在一条线程里使用**（本项目里是 demux 线程）。
class Demuxer
{
public:
    Demuxer();
    ~Demuxer();

    Demuxer(Demuxer&& other) noexcept;
    Demuxer& operator=(Demuxer&& other) noexcept;
    Demuxer(const Demuxer&)            = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    core::Status Open(const std::string& url, const DemuxOptions& options);
    void         Close();
    bool         IsOpen() const noexcept { return format_ != nullptr; }

    const MediaInfo& Info() const noexcept { return info_; }

    enum class ReadResult
    {
        Ok,
        EndOfStream,
        Cancelled,   // 被中断回调打断（seek / stop），不是错误
        Error,
    };

    // 取下一个"有用的"包（跳过未启用的流）
    ReadResult Read(Packet& out, core::Status* error = nullptr);

    // 跳转；返回后需要调用方自行 flush 解码器与队列（见 Player::SeekTo）
    core::Status SeekTo(double seconds);

    // 中断回调：返回 true 会让阻塞中的 avformat_* 立刻返回
    // （seek/退出时用它把 demux 线程从 av_read_frame 里叫醒）
    void SetInterruptCallback(std::function<bool()> callback);

    // 流索引 -> 是否为音频/视频
    bool IsAudioStream(int index) const noexcept { return index >= 0 && index == info_.audio.index; }
    bool IsVideoStream(int index) const noexcept { return index >= 0 && index == info_.video.index; }

private:
    static int InterruptThunk(void* opaque);
    void FillInfo();

    AVFormatContext*      format_ = nullptr;
    DemuxOptions          options_;
    MediaInfo             info_;
    std::function<bool()> interrupt_;
};

} // namespace av::media