#pragma once

#include "av/core/InputEvent.h"

namespace av::output {

// 输入事件源：后端把平台的原始事件（SDL_Event / QEvent / Win32 MSG）
// 翻译成 core::InputEvent，于是交互逻辑完全脱离窗口。
//
// 线程约定：只在主线程调用（Windows 上跨线程读窗口消息会收不到消息，
// 这也是旧版"播放中窗口卡死"的根因）。
class IEventSource
{
public:
    virtual ~IEventSource() = default;

    // 取一个事件；没有更多事件时返回 false
    virtual bool Poll(core::InputEvent& out) = 0;

    // 是否收到退出请求（关窗 / Alt+F4 / Esc 由上层映射）
    virtual bool QuitRequested() const = 0;
};

} // namespace av::output