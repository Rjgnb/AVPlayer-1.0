#pragma once

// 这个头文件**不包含 FFmpeg 头**（pimpl 手法）：
// 上层只看到中性类型，于是"换掉 swr"不会波及调用方。
#include <memory>

#include "av/core/Status.h"
#include "av/core/Types.h"

namespace av::media {

class Frame;

// PCM 重采样/格式转换。
//
// 倍速的实现就在这里：把输入采样率按 speed 缩放后交给 swr，
// 于是输出样本数 = 输入样本数 / speed —— 设备以原速率播放就是"磁带式变速"。
class AudioResampler
{
public:
    AudioResampler();
    ~AudioResampler();

    AudioResampler(AudioResampler&& other) noexcept;
    AudioResampler& operator=(AudioResampler&& other) noexcept;
    AudioResampler(const AudioResampler&)            = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    core::Status Configure(const core::AudioFormat& input, const core::AudioFormat& output, double speed = 1.0);
    core::Status SetSpeed(double speed);
    void         Close();
    bool         IsConfigured() const noexcept;

    double                  Speed() const noexcept;
    const core::AudioFormat& OutputFormat() const noexcept;
    const core::AudioFormat& InputFormat() const noexcept;

    // 转换一帧。返回的视图指向内部缓冲：**下一次 Convert() 之前有效**。
    core::Result<core::AudioFrameView> Convert(const Frame& frame, double ptsSeconds);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace av::media