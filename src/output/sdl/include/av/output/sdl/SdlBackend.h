#pragma once

#include <memory>

#include "av/output/Backend.h"

namespace av::output::sdl {

// SDL2 后端：一套 IBackend 实现（窗口 + 音频设备 + 事件）
//
// 线程约定（很重要）
//   * Initialize / CreateXxx / Shutdown：控制线程
//   * 窗口与渲染相关（Draw / Present / RenderTarget / Poll）：**只在主线程**
//     —— Windows 上跨线程读窗口消息会导致"播放中窗口无响应"，这是旧版踩过的坑
//   * 音频相关（Write / Pause / Flush）：只在音频线程
class SdlBackend final : public IBackend
{
public:
    SdlBackend();
    ~SdlBackend() override;

    const char* Name() const override { return "sdl"; }

    core::Status Initialize(const core::KeyValues& options) override;
    void         Shutdown() override;

    std::unique_ptr<IAudioSink>   CreateAudioSink() override;
    std::unique_ptr<IVideoSink>   CreateVideoSink() override;
    std::unique_ptr<IEventSource> CreateEventSource() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 注册到全局注册表（app 里一行调用）。
// 用显式注册而不是"静态初始化自注册"：静态库里的静态对象可能被链接器丢掉，
// 而且显式注册让"用哪个后端"始终是 app 的决定 —— 依赖方向也不会被破坏。
void RegisterBackend();

} // namespace av::output::sdl