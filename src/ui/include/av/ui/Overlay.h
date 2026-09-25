#pragma once

#include <string>

#include "av/core/InputEvent.h"
#include "av/core/Types.h"
#include "av/output/RenderTarget.h"

namespace av::ui {

// 覆盖层状态：渲染所需的全部数据（由输入 + 播放器状态更新）。
// 它是**纯数据**，不持有窗口、不调用播放器 —— 所以可以在单元测试里随意构造。
struct OverlayState
{
    bool   visible  = true;
    bool   playing  = false;
    bool   dragging = false;
    double position = 0.0;
    double duration = 0.0;
    double speed    = 1.0;

    double idleSeconds = 0.0;      // 鼠标静止时长（用于自动隐藏）
    bool   cursorInside = false;
    core::Point cursor;

    core::Rect bar;                // 进度条轨道（Layout 计算）
    core::Rect barHit;             // 进度条命中区（比轨道大，方便抓）
    core::Rect hitArea;            // 单击暂停的命中区（通常是整个画面）
};

enum class ActionType
{
    None,
    TogglePause,
    SeekTo,          // value = 目标秒数（拖拽松手 / 点击进度条）
    SeekRelative,    // value = 相对秒数（方向键）
    SetSpeed,        // value = 新倍速
    Quit,
};

struct Action
{
    ActionType type  = ActionType::None;
    double     value = 0.0;

    bool IsNone() const { return type == ActionType::None; }
};

// 键位/鼠标映射：全部可配置，于是"换一套交互方案"不需要改逻辑
struct InputMapConfig
{
    core::KeyCode togglePause  = core::KeyCode::Space;
    core::KeyCode quit         = core::KeyCode::Escape;
    core::KeyCode seekForward  = core::KeyCode::Right;
    core::KeyCode seekBackward = core::KeyCode::Left;
    core::KeyCode speedUp      = core::KeyCode::RightBracket;
    core::KeyCode speedDown    = core::KeyCode::LeftBracket;
    core::KeyCode jumpForward  = core::KeyCode::Up;      // +60s
    core::KeyCode jumpBackward = core::KeyCode::Down;    // -60s

    double seekStepSeconds    = 5.0;
    double jumpStepSeconds    = 60.0;
    bool   clickToPause       = true;
    bool   wheelAdjustsSpeed  = true;
    double barHeight          = 26.0;   // 进度条命中区高度（像素）
    double barMargin          = 40.0;
    double autoHideSeconds    = 2.5;
};

// 纯逻辑：输入 -> 动作，同时更新 state（悬停/拖拽/自动隐藏计时）。
// 没有窗口、没有播放器 —— 这就是"可测试的 UI"。
class OverlayController
{
public:
    explicit OverlayController(InputMapConfig map = {});

    Action Handle(const core::InputEvent& event, OverlayState& state);
    void   Layout(OverlayState& state, core::Size renderSize);
    void   Update(OverlayState& state, double deltaSeconds);
    bool   NeedsRedraw() const { return needsRedraw_; }
    void   ClearRedrawFlag() { needsRedraw_ = false; }

    const InputMapConfig& Mapping() const { return map_; }

private:
    Action HandleKey(const core::InputEvent& event, OverlayState& state);
    Action HandleMouse(const core::InputEvent& event, OverlayState& state);
    Action HandleWheel(const core::InputEvent& event, OverlayState& state);
    double TimeAtX(const OverlayState& state, int x) const;

    // 倍速档位：离散化，避免用户按住键变成 1.037x 这种怪数字
    double NextSpeed(double current, int direction) const;

    InputMapConfig map_;
    bool           needsRedraw_ = true;
};

// 纯绘制：把 state 画到任意 IRenderTarget（SDL 纹理 / QPainter / 内存录制器）
class OverlayRenderer
{
public:
    void Draw(const OverlayState& state, output::IRenderTarget& target);
};

} // namespace av::ui