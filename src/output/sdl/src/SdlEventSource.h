#pragma once

#include <memory>

#include "av/output/EventSource.h"

#include "SdlInternal.h"

namespace av::output::sdl {

// 内部头文件：只在 avoutput_sdl 内部使用

// 把 SDL_Event 翻译成 core::InputEvent。
// 这里是"平台细节"的唯一出口：上层（ui/player）拿到的全是中立事件，可单测。
class SdlEventSource final : public IEventSource
{
public:
    explicit SdlEventSource(std::shared_ptr<SdlContext> context);

    bool Poll(core::InputEvent& out) override;
    bool QuitRequested() const override { return quit_; }

private:
    std::shared_ptr<SdlContext> context_;
    bool                        quit_ = false;
};

} // namespace av::output::sdl