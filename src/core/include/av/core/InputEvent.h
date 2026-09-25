#pragma once

#include <string_view>

#include "av/core/Types.h"

namespace av::core {

// 键码：后端（SDL/Windows/Qt）把各自的键翻译成这套中立枚举，
// 于是"空格暂停""方向键跳转"这类交互逻辑可以脱离窗口做单元测试。
enum class KeyCode
{
    Unknown = 0,
    Space,
    Escape,
    Enter,
    Tab,
    Backspace,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    LeftBracket,
    RightBracket,
    Minus,
    Equal,
    Comma,
    Period,
    Slash,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

enum class MouseButton
{
    None = 0,
    Left,
    Middle,
    Right,
};

enum class InputEventType
{
    None = 0,
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    Quit,
    WindowResized,
    WindowFocusLost,
};

struct InputEvent
{
    InputEventType type = InputEventType::None;
    KeyCode        key  = KeyCode::Unknown;
    bool           isRepeat = false;
    MouseButton    button   = MouseButton::None;
    Point          position;          // 窗口坐标系
    int            wheelDelta = 0;    // 正 = 向上/远离用户
    Size           windowSize;        // 仅 WindowResized 有效

    bool IsKey() const noexcept
    {
        return type == InputEventType::KeyDown || type == InputEventType::KeyUp;
    }
    bool IsMouseButton() const noexcept
    {
        return type == InputEventType::MouseButtonDown || type == InputEventType::MouseButtonUp;
    }
};

const char* ToString(InputEventType type) noexcept;
const char* ToString(KeyCode key) noexcept;
const char* ToString(MouseButton button) noexcept;

} // namespace av::core