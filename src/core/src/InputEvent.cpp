#include "av/core/InputEvent.h"

#include <cstdio>

namespace av::core {

const char* ToString(InputEventType type) noexcept
{
    switch (type)
    {
    case InputEventType::None:            return "None";
    case InputEventType::KeyDown:         return "KeyDown";
    case InputEventType::KeyUp:           return "KeyUp";
    case InputEventType::MouseMove:       return "MouseMove";
    case InputEventType::MouseButtonDown: return "MouseButtonDown";
    case InputEventType::MouseButtonUp:   return "MouseButtonUp";
    case InputEventType::MouseWheel:      return "MouseWheel";
    case InputEventType::Quit:            return "Quit";
    case InputEventType::WindowResized:   return "WindowResized";
    case InputEventType::WindowFocusLost: return "WindowFocusLost";
    }
    return "Unknown";
}

const char* ToString(MouseButton button) noexcept
{
    switch (button)
    {
    case MouseButton::None:   return "None";
    case MouseButton::Left:   return "Left";
    case MouseButton::Middle: return "Middle";
    case MouseButton::Right:  return "Right";
    }
    return "Unknown";
}

const char* ToString(KeyCode key) noexcept
{
    switch (key)
    {
    case KeyCode::Unknown:     return "Unknown";
    case KeyCode::Space:       return "Space";
    case KeyCode::Escape:      return "Escape";
    case KeyCode::Enter:       return "Enter";
    case KeyCode::Tab:         return "Tab";
    case KeyCode::Backspace:   return "Backspace";
    case KeyCode::Left:        return "Left";
    case KeyCode::Right:       return "Right";
    case KeyCode::Up:          return "Up";
    case KeyCode::Down:        return "Down";
    case KeyCode::Home:        return "Home";
    case KeyCode::End:         return "End";
    case KeyCode::PageUp:      return "PageUp";
    case KeyCode::PageDown:    return "PageDown";
    case KeyCode::LeftBracket: return "LeftBracket";
    case KeyCode::RightBracket:return "RightBracket";
    case KeyCode::Minus:       return "Minus";
    case KeyCode::Equal:       return "Equal";
    case KeyCode::Comma:       return "Comma";
    case KeyCode::Period:      return "Period";
    case KeyCode::Slash:       return "Slash";
    default: break;
    }

    // 连续区间用算术生成，避免几十个 case
    static thread_local char buffer[8];
    const int value = static_cast<int>(key);

    if (key >= KeyCode::Digit0 && key <= KeyCode::Digit9)
    {
        buffer[0] = static_cast<char>('0' + (value - static_cast<int>(KeyCode::Digit0)));
        buffer[1] = '\0';
        return buffer;
    }
    if (key >= KeyCode::A && key <= KeyCode::Z)
    {
        buffer[0] = static_cast<char>('A' + (value - static_cast<int>(KeyCode::A)));
        buffer[1] = '\0';
        return buffer;
    }
    if (key >= KeyCode::F1 && key <= KeyCode::F12)
    {
        std::snprintf(buffer, sizeof(buffer), "F%d", 1 + (value - static_cast<int>(KeyCode::F1)));
        return buffer;
    }
    return "Unknown";
}

} // namespace av::core