#pragma once

// 控制台宿主里的"胶水层"：
//   Player 只负责播放，ui 只负责算动作/画覆盖层，
//   把两者接起来的那几行属于 app —— 换个宿主（Qt）也只需要换这几行。
#include <cmath>
#include <iostream>
#include <string>

#include "av/Player.h"
#include "av/core/StringFormat.h"
#include "av/ui/Overlay.h"

namespace app {

class ConsoleHud final : public av::PlayerObserver
{
public:
    explicit ConsoleHud(av::Player& player) : player_(player), controller_(MakeMapping())
    {
        player_.SetOverlayPainter([this](av::output::IRenderTarget& target) {
            renderer_.Draw(state_, target);
        });
        player_.SetObserver(this);
    }

    // 每帧：自动隐藏计时 + 把界面状态对齐到"唯一真值"（播放器）
    void Tick(double deltaSeconds)
    {
        controller_.Update(state_, deltaSeconds);

        // 覆盖层几何必须跟"实际绘制区"一致：缩放窗口、高 DPI、视频尺寸变化都会改它。
        // 每帧比一次比"监听 WindowResized 事件"更可靠 —— 事件只是变化的一种来源。
        const av::core::Size renderSize = player_.RenderSize();
        if (renderSize.width > 0 && renderSize.height > 0 && renderSize != lastRenderSize_)
        {
            lastRenderSize_ = renderSize;
            controller_.Layout(state_, renderSize);
        }
    }

    // 输入：ui 算动作 -> app 施加动作
    void OnInputEvent(const av::core::InputEvent& event) override
    {
        SyncFromPlayer();
        Apply(controller_.Handle(event, state_));
    }

    void OnMediaOpened(const av::MediaDescription& media) override
    {
        state_.duration = media.durationSeconds;
        std::cout << "媒体: " << media.container << " / " << media.codecSummary
                  << " 时长=" << av::core::FormatTimecode(media.durationSeconds)
                  << (media.hasVideo ? " [视频]" : "") << (media.hasAudio ? "[音频]" : "") << "\n";
    }

    void OnStateChanged(av::PlayerState state) override
    {
        state_.playing = (state == av::PlayerState::Playing);
        std::cout << "状态: " << av::ToString(state) << "\n";
    }

    void OnPositionChanged(double seconds) override
    {
        if (!state_.dragging) state_.position = seconds;
    }

    void OnError(const av::core::Status& status) override
    {
        std::cerr << "错误: " << status.ToString() << "\n";
    }

    void OnEnded() override
    {
        std::cout << "播放结束\n";
    }

    const av::ui::OverlayState& State() const { return state_; }

private:
    // 界面状态不自己"记"播放器的事实：每次处理输入前同步一次，避免两份真值打架。
    // （否则用 --speed 2 起播时界面还以为是 1.0，按一下 [ 会直接跳到 0.75）
    void SyncFromPlayer()
    {
        state_.playing = player_.IsPlaying();
        state_.speed   = player_.Speed();
        if (state_.duration <= 0.0) state_.duration = player_.Duration();
        if (!state_.dragging) state_.position = player_.Position();
    }

    static av::ui::InputMapConfig MakeMapping()
    {
        av::ui::InputMapConfig map;
        map.clickToPause = true;
        return map;
    }

    void Apply(const av::ui::Action& action)
    {
        switch (action.type)
        {
        case av::ui::ActionType::TogglePause:
            player_.TogglePlayPause();
            break;
        case av::ui::ActionType::SeekTo:
            player_.SeekTo(action.value);
            state_.position = action.value;
            std::cout << "跳转到 " << av::core::FormatTimecode(action.value) << "\n";
            break;
        case av::ui::ActionType::SeekRelative:
        {
            double target = player_.Position() + action.value;
            if (target < 0.0) target = 0.0;
            player_.SeekTo(target);
            std::cout << (action.value >= 0 ? "前进 " : "后退 ") << std::fabs(action.value) << "s\n";
            break;
        }
        case av::ui::ActionType::SetSpeed:
            player_.SetSpeed(action.value);
            state_.speed = action.value;
            std::cout << "倍速 " << av::core::FormatSpeed(action.value) << "\n";
            break;
        case av::ui::ActionType::Quit:
            player_.RequestQuit();
            break;
        case av::ui::ActionType::None:
            break;
        }
    }

    av::Player&               player_;
    av::ui::OverlayController controller_;
    av::ui::OverlayRenderer   renderer_;
    av::ui::OverlayState      state_;
    av::core::Size            lastRenderSize_;
};

} // namespace app
