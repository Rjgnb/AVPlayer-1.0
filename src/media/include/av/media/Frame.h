#pragma once

#include "av/media/FFmpegCompat.h"

#include "av/core/Rational.h"
#include "av/core/Types.h"

namespace av::media {

// AVFrame 的 RAII 包装（只有移动语义）
class Frame
{
public:
    // 默认构造 = **空帧，不分配 AVFrame**。
    // 这条很重要：像 Tagged<Frame> 这样的"哨兵/占位"对象不该偷偷分配一个 AVFrame，
    // 否则 IsAllocated() 会被误读成"有数据" —— 播放器里就出现过
    // "pendingFrame = {} 之后 IsAllocated() 仍然为 true" 的诡异 bug
    // （表现：永远在用空帧的 pts=0，疯狂丢帧、播不到结尾）。
    Frame() = default;

    // 需要"能接住解码结果"的容器时才分配（解码器内部用）
    explicit Frame(core::Rational timeBase);
    ~Frame();

    Frame(Frame&& other) noexcept;
    Frame& operator=(Frame&& other) noexcept;
    Frame(const Frame&)            = delete;
    Frame& operator=(const Frame&) = delete;

    // 容器是否存在（av_frame_alloc 成功过）
    bool     IsAllocated() const noexcept { return frame_ != nullptr; }
    // **真的有数据**：解码器已经填过内容。空帧、被移走的帧都是 false。
    // 想判断"有没有一帧可以呈现/处理"时用这个，而不是 IsAllocated()。
    bool     HasData() const noexcept { return frame_ != nullptr && frame_->data[0] != nullptr; }
    AVFrame* Raw() const noexcept { return frame_; }

    core::Rational TimeBase() const noexcept { return timeBase_; }

    // 显示时间（秒）：优先 best_effort_timestamp，其次 pts
    double PtsSeconds() const noexcept;
    // 帧时长（秒）：优先 pkt_duration，其次按帧率/采样率都没有时返回 0
    double DurationSeconds() const noexcept;

    void Release() { if (frame_) av_frame_unref(frame_); }

private:
    AVFrame*       frame_ = nullptr;
    core::Rational timeBase_;
};

// 由"帧 + 格式"构造只读视图（零拷贝）
core::VideoFrameView MakeVideoView(const Frame& frame, const core::VideoFormat& format, double durationSeconds = 0.0);
core::AudioFrameView MakeAudioView(const Frame& frame, const core::AudioFormat& format);

// 从解码帧读取"真实格式"。
// 为什么需要它：容器里写的像素/采样格式只是"预期值"，
// 解码器实际吐出来的格式要以帧为准（这是很多播放器 bug 的来源）。
core::VideoFormat VideoFormatOf(const Frame& frame, core::PixelFormat* outPixelFormat = nullptr);
core::AudioFormat AudioFormatOf(const Frame& frame);

} // namespace av::media
