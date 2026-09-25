#pragma once

#include <cstddef>
#include <string>

#include "av/core/Status.h"
#include "av/core/Types.h"

namespace av::output {

struct AudioSinkConfig
{
    std::string device;                   // 空 = 默认设备
    int         bufferSamples = 1024;     // 设备缓冲（越小延迟越低，越容易欠载）
    double      watermarkSeconds = 0.30;  // 只作为参考值；背压由 pipeline 依据时钟换算执行
    bool        allowResample = true;     // 后端允许"改变格式"（返回 NegotiateFormat 的结果）
};

// 音频输出设备抽象。
//
// 线程约定：Open/Close 在控制线程；Write/Pause/Flush 只在音频线程；
//          QueuedBytes 可以被任何线程读取。
class IAudioSink
{
public:
    virtual ~IAudioSink() = default;

    virtual const char* Name() const = 0;

    // 后端希望实际打开的格式（通常等于 source；后端不支持时才降级）。
    // pipeline 会用 swr 把解码帧转换成这个格式后再 Write。
    virtual core::AudioFormat NegotiateFormat(const core::AudioFormat& source) const = 0;

    virtual core::Status Open(const core::AudioFormat& format, const AudioSinkConfig& config) = 0;
    virtual void         Close() = 0;
    virtual bool         IsOpen() const = 0;

    // 写入交错 PCM。返回前数据必须已被"接纳"（拷贝进设备队列）。
    virtual core::Status Write(const core::AudioFrameView& pcm) = 0;

    // 设备里还没播完的字节数 —— 音频主时钟就是靠它反推当前媒体时间的
    virtual std::size_t QueuedBytes() const = 0;

    virtual void Pause(bool paused) = 0;
    virtual void Flush() = 0;                 // 跳转：丢掉已排队但未播出的数据
};

} // namespace av::output