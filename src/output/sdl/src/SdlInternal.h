#pragma once

// SDL 后端内部共享的东西（不进 include/，外面看不到）
#include <memory>
#include <mutex>

#include <SDL.h>

#include "av/core/InputEvent.h"
#include "av/core/Types.h"
#include "av/output/VideoSink.h"

namespace av::output::sdl {

// SDL 子系统的引用计数（多个后端实例/播放器不会互相把 SDL_Quit 掉）
core::Status   AcquireSdl();
void           ReleaseSdl();

core::KeyCode  MapKeyCode(SDL_Keycode key);
SDL_AudioFormat ToSdlAudioFormat(core::SampleFormat format);
Uint32         ToSdlPixelFormat(core::PixelFormat format);

// 窗口 + 渲染器 + 视频纹理。**只允许主线程访问**。
class SdlWindow
{
public:
    ~SdlWindow();

    core::Status Create(const core::VideoFormat& format, const VideoSinkConfig& config);
    void         Destroy();
    bool         IsValid() const { return window_ != nullptr && renderer_ != nullptr; }

    // 渲染目标尺寸（**像素**）。高 DPI 下它比"窗口坐标点"大：渲染与命中判定都必须用像素，
    // 否则 Windows 缩放 125%/150% 时覆盖层与进度条会画到别处去。
    core::Size    ClientSize() const;
    // 窗口坐标（SDL 事件用的单位）-> 渲染目标坐标（像素）
    core::Point   ToRenderPoint(core::Point windowPoint) const;
    void          SetTitle(const std::string& title);

    SDL_Renderer* Renderer() const { return renderer_; }

    // 一帧的绘制：清屏 -> 视频（等比缩放居中）-> 覆盖层（由 SdlRenderTarget 画）
    void Draw(const core::VideoFrameView& frame);
    void Present();

private:
    core::Status EnsureTexture(const core::VideoFormat& format);
    SDL_Rect     DestinationRect() const;

    SDL_Window*       window_   = nullptr;
    SDL_Renderer*     renderer_ = nullptr;
    SDL_Texture*      texture_  = nullptr;
    core::VideoFormat textureFormat_;
};

// 后端内部共享状态：窗口由"视频 sink 创建、事件源也能看到"
class SdlContext
{
public:
    std::shared_ptr<SdlWindow> Window();
    std::shared_ptr<SdlWindow> EnsureWindow(const VideoSinkConfig& config);
    void                       ResetWindow();

private:
    std::mutex                 mutex_;
    std::shared_ptr<SdlWindow> window_;
};

} // namespace av::output::sdl
