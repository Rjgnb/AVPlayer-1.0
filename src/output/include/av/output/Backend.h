#pragma once

#include <memory>
#include <string_view>

#include "av/core/KeyValues.h"
#include "av/core/Status.h"
#include "av/output/AudioSink.h"
#include "av/output/EventSource.h"
#include "av/output/VideoSink.h"

namespace av::output {

// 一个"输出后端"= 音频设备 + 视频窗口 + 事件源 的一套实现（SDL / Qt / ALSA / 假后端...）
//
// 这就是"硬件驱动可替换"的落点：上层只认 IBackend，
// 换后端 = 换一个实现 + 改一行注册，核心代码零改动。
class IBackend
{
public:
    virtual ~IBackend() = default;

    // 稳定的后端名（"sdl" / "null" / "qt"），用于配置与命令行选择
    virtual const char* Name() const = 0;

    // 初始化全局资源（如 SDL_Init）；options 来自 PlayerConfig::backendOptions
    virtual core::Status Initialize(const core::KeyValues& options) = 0;
    virtual void         Shutdown() = 0;

    virtual std::unique_ptr<IAudioSink>   CreateAudioSink() = 0;
    virtual std::unique_ptr<IVideoSink>   CreateVideoSink() = 0;
    virtual std::unique_ptr<IEventSource> CreateEventSource() = 0;
};

} // namespace av::output