#pragma once

#include <cstddef>
#include <mutex>

#include "av/core/Clock.h"
#include "av/core/Rational.h"

namespace av::core {

// 播放时钟：全套代码里唯一的"时间权威"
//
//   音频主时钟：媒体时间 = 最后写入设备的音频 pts - 设备里还没播完的字节折算的时间
//   墙钟兜底  ：没有音频流时，由主线程每帧 TickWall() 推进
//
// 倍速只在这一个地方体现：折算公式里乘 speed，于是"视频比音频快/慢"自动成立，
// 视频侧的同步逻辑一个字都不用改。
class MediaClock
{
public:
    enum class Source
    {
        Audio,   // 音频是主时钟（有音频流时）
        Wall,    // 墙钟兜底（无音频流）
    };

    explicit MediaClock(const IClock* wall = nullptr);

    void   SetSource(Source source);
    Source GetSource() const;

    void   SetSpeed(double speed);          // 限幅在 [0.05, 16]
    double Speed() const;

    // PCM 字节率 = sampleRate * channels * bytesPerSample
    void   SetByteRate(double bytesPerSecond);
    double ByteRate() const;

    // 开播 / 跳转：把时间基准设为 mediaSeconds
    void Reset(double mediaSeconds);

    void Pause(bool paused);                 // 仅影响 Wall 源；音频源靠设备暂停自然冻结
    bool IsPaused() const;

    // 主线程每帧调用（Wall 源推进）。返回推进后的媒体时间。
    double TickWall();

    // 音频线程写完设备队列后调用
    void NotifyAudioWritten(double ptsSeconds);
    double LastAudioPts() const;

    double AudioNow(std::size_t queuedBytes) const;   // 音频主时钟的"当前媒体时间"
    double WallNow() const;

    // 统一的查询入口：调用方不必关心当前是哪种源
    double Now(std::size_t queuedBytes) const;

    // 设备排队水位的"字节阈值"（倍速下自动换算）
    double WatermarkBytes(double mediaSeconds) const;
    // 排队字节折算成媒体秒数（诊断用）
    double QueuedSeconds(std::size_t queuedBytes) const;

    void MarkEnded(bool ended);
    bool IsEnded() const;

private:
    const IClock* wallClock_ = nullptr;
    mutable std::mutex mutex_;
    Source    source_     = Source::Audio;
    double    speed_      = 1.0;
    double    byteRate_   = 0.0;
    double    baseMedia_  = 0.0;    // seek 起点
    double    lastAudioPts_ = 0.0;
    bool      hasAudio_   = false;
    double    wallAnchor_ = 0.0;    // 上次 TickWall 的墙钟
    double    wallMedia_  = 0.0;    // 与之对应的媒体时间
    bool      paused_     = false;
    bool      ended_      = false;
};

} // namespace av::core