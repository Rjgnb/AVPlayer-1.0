#pragma once

#include <string>
#include <string_view>

#include "av/core/Status.h"
#include "av/core/Types.h"
#include "av/output/RenderTarget.h"

namespace av::output {

struct VideoSinkConfig
{
    std::string title;
    bool        vsync     = true;
    bool        resizable = true;
    bool        highDpi   = true;
    int         windowWidth  = 0;   // 0 = 用视频尺寸
    int         windowHeight = 0;
};

// 视频输出（窗口 + 呈现）抽象。
//
// 【一帧的调用顺序】Draw() -> 用 RenderTarget() 画覆盖层 -> Present()
//   Draw    ：把视频帧画进后台缓冲（可能触发像素格式转换/纹理上传）
//   Present ：提交到屏幕（SDL_RenderPresent / QWidget::update）
// 拆成两步的原因：覆盖层必须画在视频之上、但要画在同一块后台缓冲上。
class IVideoSink
{
public:
    virtual ~IVideoSink() = default;

    virtual const char* Name() const = 0;

    // 能力协商：pipeline 决定"直通"还是"用 sws 转成 PreferredFormat()"
    virtual bool               SupportsFormat(core::PixelFormat format) const = 0;
    virtual core::PixelFormat  PreferredFormat() const = 0;

    virtual core::Status Open(const core::VideoFormat& sourceFormat, const VideoSinkConfig& config) = 0;
    virtual void         Close() = 0;
    virtual bool         IsOpen() const = 0;

    virtual core::Status Draw(const core::VideoFrameView& frame) = 0;
    virtual void         Present() = 0;

    // 覆盖层绘制目标。
    // 契约：**只有 Open() 之后才非空**；没有可绘制区域（headless 后端 / 未打开）返回 nullptr。
    // 调用方必须判空 —— 后端自己也要守这条约定，否则覆盖层会被画到一块并不存在的画布上。
    virtual IRenderTarget* RenderTarget() = 0;

    // 实际可绘制区域（像素）
    virtual core::Size RenderSize() const = 0;

    virtual void SetTitle(std::string_view title) { (void)title; }
};

} // namespace av::output
