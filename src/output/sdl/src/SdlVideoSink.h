#pragma once

#include <memory>
#include <string_view>

#include "av/output/VideoSink.h"

#include "SdlInternal.h"

namespace av::output::sdl {

// 内部头文件：只在 avoutput_sdl 内部使用（外部只应看到 SdlBackend.h）

// SDL 视频输出：把视频帧喂给 YUV/RGB 纹理，等比缩放居中显示。
// 覆盖层画在同一块后台缓冲上（Draw 之后、Present 之前）。
class SdlVideoSink final : public IVideoSink
{
public:
    explicit SdlVideoSink(std::shared_ptr<SdlContext> context);
    ~SdlVideoSink() override;

    const char* Name() const override { return "sdl-video"; }

    bool              SupportsFormat(core::PixelFormat format) const override;
    core::PixelFormat PreferredFormat() const override { return core::PixelFormat::Yuv420P; }

    core::Status Open(const core::VideoFormat& sourceFormat, const VideoSinkConfig& config) override;
    void         Close() override;
    bool         IsOpen() const override;

    core::Status Draw(const core::VideoFrameView& frame) override;
    void         Present() override;

    IRenderTarget* RenderTarget() override;
    core::Size     RenderSize() const override;

    void SetTitle(std::string_view title) override;

private:
    class SdlRenderTarget;

    std::shared_ptr<SdlContext> context_;
    std::shared_ptr<SdlWindow>  window_;
    std::unique_ptr<SdlRenderTarget> renderTarget_;
    bool                             open_ = false;
};

} // namespace av::output::sdl