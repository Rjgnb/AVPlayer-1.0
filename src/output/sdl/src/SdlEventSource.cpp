#include "SdlEventSource.h"

#include "SdlInternal.h"

#include "av/core/Log.h"

namespace av::output::sdl {

namespace {
constexpr const char* kTag = "sdl-event";
}

core::KeyCode MapKeyCode(SDL_Keycode key)
{
    using core::KeyCode;

    // 字母/数字/F 键：连续区间用算术映射，别写 50 个 case
    if (key >= SDLK_a && key <= SDLK_z) return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (key - SDLK_a));
    if (key >= SDLK_0 && key <= SDLK_9) return static_cast<KeyCode>(static_cast<int>(KeyCode::Digit0) + (key - SDLK_0));
    if (key >= SDLK_F1 && key <= SDLK_F12) return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + (key - SDLK_F1));

    switch (key)
    {
    case SDLK_SPACE:      return KeyCode::Space;
    case SDLK_ESCAPE:     return KeyCode::Escape;
    case SDLK_RETURN:     return KeyCode::Enter;
    case SDLK_TAB:        return KeyCode::Tab;
    case SDLK_BACKSPACE:  return KeyCode::Backspace;
    case SDLK_LEFT:       return KeyCode::Left;
    case SDLK_RIGHT:      return KeyCode::Right;
    case SDLK_UP:         return KeyCode::Up;
    case SDLK_DOWN:       return KeyCode::Down;
    case SDLK_HOME:       return KeyCode::Home;
    case SDLK_END:        return KeyCode::End;
    case SDLK_PAGEUP:     return KeyCode::PageUp;
    case SDLK_PAGEDOWN:   return KeyCode::PageDown;
    case SDLK_LEFTBRACKET:  return KeyCode::LeftBracket;
    case SDLK_RIGHTBRACKET: return KeyCode::RightBracket;
    case SDLK_MINUS:      return KeyCode::Minus;
    case SDLK_EQUALS:     return KeyCode::Equal;
    case SDLK_COMMA:      return KeyCode::Comma;
    case SDLK_PERIOD:     return KeyCode::Period;
    case SDLK_SLASH:      return KeyCode::Slash;
    default:              return KeyCode::Unknown;
    }
}

SdlEventSource::SdlEventSource(std::shared_ptr<SdlContext> context) : context_(std::move(context)) {}

bool SdlEventSource::Poll(core::InputEvent& out)
{
    // 鼠标/窗口事件在 SDL 里是"窗口坐标"，而渲染与覆盖层用"像素"。
    // 高 DPI 下两者不同，所以这里统一换算 —— 平台怪癖只留在后端内部，
    // 上层（ui/player）永远只看到像素坐标。
    const std::shared_ptr<SdlWindow> window = context_ ? context_->Window() : nullptr;
    const auto toRender = [&window](int x, int y) {
        const core::Point point{ x, y };
        return window ? window->ToRenderPoint(point) : point;
    };

    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0)
    {
        switch (event.type)
        {
        case SDL_QUIT:
            quit_ = true;
            out.type = core::InputEventType::Quit;
            return true;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            out.type     = event.type == SDL_KEYDOWN ? core::InputEventType::KeyDown : core::InputEventType::KeyUp;
            out.key      = MapKeyCode(event.key.keysym.sym);
            out.isRepeat = event.key.repeat != 0;
            if (out.key == core::KeyCode::Unknown) continue;   // 未映射的键：丢弃，不上报
            return true;

        case SDL_MOUSEMOTION:
            out.type     = core::InputEventType::MouseMove;
            out.position = toRender(event.motion.x, event.motion.y);
            return true;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            out.type   = event.type == SDL_MOUSEBUTTONDOWN ? core::InputEventType::MouseButtonDown
                                                           : core::InputEventType::MouseButtonUp;
            out.button = event.button.button == SDL_BUTTON_LEFT   ? core::MouseButton::Left
                       : event.button.button == SDL_BUTTON_MIDDLE ? core::MouseButton::Middle
                       : event.button.button == SDL_BUTTON_RIGHT  ? core::MouseButton::Right
                                                                  : core::MouseButton::None;
            out.position = toRender(event.button.x, event.button.y);
            return true;

        case SDL_MOUSEWHEEL:
        {
            int mouseX = 0;
            int mouseY = 0;
            SDL_GetMouseState(&mouseX, &mouseY);
            out.type       = core::InputEventType::MouseWheel;
            out.wheelDelta = event.wheel.y;
            out.position   = toRender(mouseX, mouseY);
            return true;
        }

        case SDL_WINDOWEVENT:
            switch (event.window.event)
            {
            case SDL_WINDOWEVENT_CLOSE:
                quit_ = true;
                out.type = core::InputEventType::Quit;
                return true;
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                out.type       = core::InputEventType::WindowResized;
                out.windowSize = window ? window->ClientSize()
                                        : core::Size{ event.window.data1, event.window.data2 };
                return true;
            case SDL_WINDOWEVENT_FOCUS_LOST:
                out.type = core::InputEventType::WindowFocusLost;
                return true;
            default:
                continue;   // 其它窗口事件（曝光/移动）对播放逻辑无意义
            }

        default:
            continue;
        }
    }
    return false;
}

} // namespace av::output::sdl
