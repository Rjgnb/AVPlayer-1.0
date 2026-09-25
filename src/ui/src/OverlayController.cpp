#include "av/ui/Overlay.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace av::ui {

namespace {

// 离散档位：避免"按住键一路滑到 1.037x"这种难以理解的速度
constexpr std::array<double, 9> kSpeedSteps = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0 };

double Clamp01(double value)
{
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

} // namespace

OverlayController::OverlayController(InputMapConfig map) : map_(map) {}

void OverlayController::Layout(OverlayState& state, core::Size renderSize)
{
    state.hitArea = core::Rect{ 0, 0, renderSize.width, renderSize.height };

    const int margin = static_cast<int>(map_.barMargin);
    const int maxWidth = renderSize.width - margin * 2;
    const int width = maxWidth > 80 ? maxWidth : (renderSize.width > 40 ? renderSize.width - 20 : 0);
    const int height = 6;
    const int y = renderSize.height - margin;
    state.bar = core::Rect{ margin, y, width, height };

    const int hitHeight = static_cast<int>(map_.barHeight);
    state.barHit = core::Rect{ margin - 10, y - hitHeight / 2 + height / 2, width + 20, hitHeight };
}

void OverlayController::Update(OverlayState& state, double deltaSeconds)
{
    if (deltaSeconds <= 0.0) return;
    state.idleSeconds += deltaSeconds;
    if (!state.dragging && map_.autoHideSeconds > 0.0 && state.idleSeconds > map_.autoHideSeconds)
    {
        if (state.visible) needsRedraw_ = true;
        state.visible = false;
    }
}

double OverlayController::TimeAtX(const OverlayState& state, int x) const
{
    if (state.bar.width <= 0 || state.duration <= 0.0) return state.position;
    const double ratio = Clamp01(static_cast<double>(x - state.bar.x) / static_cast<double>(state.bar.width));
    return ratio * state.duration;
}

double OverlayController::NextSpeed(double current, int direction) const
{
    if (direction == 0) return current;

    if (direction > 0)
    {
        for (const double step : kSpeedSteps)
        {
            if (step > current + 1e-6) return step;
        }
        return kSpeedSteps.back();
    }

    for (auto it = kSpeedSteps.rbegin(); it != kSpeedSteps.rend(); ++it)
    {
        if (*it < current - 1e-6) return *it;
    }
    return kSpeedSteps.front();
}

Action OverlayController::Handle(const core::InputEvent& event, OverlayState& state)
{
    needsRedraw_ = true;

    // 任何输入都让覆盖层"醒过来"
    state.idleSeconds = 0.0;
    if (!state.visible)
    {
        state.visible = true;
        if (event.type == core::InputEventType::MouseMove) return Action{};   // 这一下只用于唤醒
    }

    switch (event.type)
    {
    case core::InputEventType::KeyDown:
        if (event.isRepeat) return Action{};
        return HandleKey(event, state);

    case core::InputEventType::MouseMove:
    {
        state.cursor = event.position;
        state.cursorInside = core::Contains(state.hitArea, event.position);
        if (state.dragging)
        {
            state.position = TimeAtX(state, event.position.x);   // 拖拽时只动 UI，不发 seek
        }
        return Action{};
    }

    case core::InputEventType::MouseButtonDown:
    {
        if (event.button != core::MouseButton::Left) return Action{};
        state.cursor = event.position;

        if (core::Contains(state.barHit, event.position))
        {
            state.dragging = true;
            state.position = TimeAtX(state, event.position.x);
            return Action{};   // 松手时才 seek：避免拖拽期间狂发 seek
        }
        if (map_.clickToPause && core::Contains(state.hitArea, event.position))
        {
            return Action{ ActionType::TogglePause, 0.0 };
        }
        return Action{};
    }

    case core::InputEventType::MouseButtonUp:
    {
        if (event.button != core::MouseButton::Left) return Action{};
        if (!state.dragging) return Action{};
        state.dragging = false;
        state.position = TimeAtX(state, event.position.x);
        return Action{ ActionType::SeekTo, state.position };
    }

    case core::InputEventType::MouseWheel:
        return HandleWheel(event, state);

    case core::InputEventType::WindowFocusLost:
        // 失焦时取消拖拽，否则回来时"鼠标明明松了，进度条还在拖"
        state.dragging = false;
        return Action{};

    case core::InputEventType::Quit:
        return Action{ ActionType::Quit, 0.0 };

    default:
        return Action{};
    }
}

Action OverlayController::HandleKey(const core::InputEvent& event, OverlayState& state)
{
    (void)state;
    if (event.key == map_.togglePause)  return Action{ ActionType::TogglePause, 0.0 };
    if (event.key == map_.quit)         return Action{ ActionType::Quit, 0.0 };
    if (event.key == map_.seekForward)  return Action{ ActionType::SeekRelative, map_.seekStepSeconds };
    if (event.key == map_.seekBackward) return Action{ ActionType::SeekRelative, -map_.seekStepSeconds };
    if (event.key == map_.jumpForward)  return Action{ ActionType::SeekRelative, map_.jumpStepSeconds };
    if (event.key == map_.jumpBackward) return Action{ ActionType::SeekRelative, -map_.jumpStepSeconds };
    if (event.key == map_.speedUp)      return Action{ ActionType::SetSpeed, NextSpeed(state.speed, +1) };
    if (event.key == map_.speedDown)    return Action{ ActionType::SetSpeed, NextSpeed(state.speed, -1) };
    return Action{};
}

Action OverlayController::HandleWheel(const core::InputEvent& event, OverlayState& state)
{
    if (!map_.wheelAdjustsSpeed || event.wheelDelta == 0) return Action{};
    const int direction = event.wheelDelta > 0 ? +1 : -1;
    return Action{ ActionType::SetSpeed, NextSpeed(state.speed, direction) };
}

} // namespace av::ui