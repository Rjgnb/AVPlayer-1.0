#include "SdlVideoSink.h"

#include "SdlInternal.h"

#include "av/core/Log.h"

namespace av::output::sdl {

namespace {
constexpr const char* kTag = "sdl-video";

std::size_t BytesPerPixel(core::PixelFormat format)
{
    switch (format)
    {
    case core::PixelFormat::Bgra:
    case core::PixelFormat::Rgba: return 4;
    default: return 0;
    }
}

} // namespace

// 覆盖层画在"后台缓冲"上：只用到矩形和线段两个原语。
// 注意：嵌套类的定义必须在命名空间之外（不能在匿名命名空间里定义成员类）。
class SdlVideoSink::SdlRenderTarget final : public IRenderTarget
{
public:
    void SetRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }
    void SetSize(core::Size size) { size_ = size; }

    core::Size Size() const override { return size_; }

    void FillRects(const core::Rect* rects, std::size_t count, core::Color color) override
    {
        if (renderer_ == nullptr) return;
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        for (std::size_t i = 0; i < count; ++i)
        {
            const SDL_Rect rect{ rects[i].x, rects[i].y, rects[i].width, rects[i].height };
            SDL_RenderFillRect(renderer_, &rect);
        }
    }

    void DrawLines(const core::Line* lines, std::size_t count, core::Color color, int thickness) override
    {
        if (renderer_ == nullptr) return;
        if (thickness < 1) thickness = 1;

        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        for (std::size_t i = 0; i < count; ++i)
        {
            for (int offset = 0; offset < thickness; ++offset)
            {
                SDL_RenderDrawLine(renderer_,
                                   lines[i].a.x + offset, lines[i].a.y + offset,
                                   lines[i].b.x + offset, lines[i].b.y + offset);
            }
        }
    }

private:
    SDL_Renderer* renderer_ = nullptr;
    core::Size    size_;
};

SdlVideoSink::SdlVideoSink(std::shared_ptr<SdlContext> context)
    : context_(std::move(context)), renderTarget_(std::make_unique<SdlRenderTarget>())
{
}

SdlVideoSink::~SdlVideoSink()
{
    Close();
}

bool SdlVideoSink::SupportsFormat(core::PixelFormat format) const
{
    return ToSdlPixelFormat(format) != SDL_PIXELFORMAT_UNKNOWN;
}

core::Status SdlVideoSink::Open(const core::VideoFormat& sourceFormat, const VideoSinkConfig& config)
{
    if (!sourceFormat.IsValid())
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "视频格式无效");
    }
    if (context_ == nullptr)
    {
        return core::Status::Error(core::StatusCode::Internal, "SdlVideoSink 缺少后端上下文");
    }

    Close();

    window_ = context_->EnsureWindow(config);
    if (!window_)
    {
        return core::Status::Error(core::StatusCode::Internal, "无法创建 SDL 窗口");
    }

    const core::Status status = window_->Create(sourceFormat, config);
    if (!status.ok()) return status;

    renderTarget_->SetRenderer(window_->Renderer());
    renderTarget_->SetSize(window_->ClientSize());
    open_ = true;
    return core::Status::Ok();
}

void SdlVideoSink::Close()
{
    if (window_)
    {
        renderTarget_->SetRenderer(nullptr);
        window_->Destroy();
        window_.reset();
    }
    open_ = false;
}

bool SdlVideoSink::IsOpen() const
{
    return open_ && window_ && window_->IsValid();
}

core::Status SdlVideoSink::Draw(const core::VideoFrameView& frame)
{
    if (!IsOpen())
    {
        return core::Status::Error(core::StatusCode::Backend, "视频输出未打开");
    }

    if (frame.format.format == core::PixelFormat::Bgra || frame.format.format == core::PixelFormat::Rgba)
    {
        if (frame.strides[0] < frame.format.width * static_cast<int>(BytesPerPixel(frame.format.format)))
        {
            return core::Status::Error(core::StatusCode::InvalidArgument, "视频帧 stride 异常");
        }
    }

    window_->Draw(frame);
    renderTarget_->SetSize(window_->ClientSize());
    return core::Status::Ok();
}

void SdlVideoSink::Present()
{
    if (window_) window_->Present();
}

IRenderTarget* SdlVideoSink::RenderTarget()
{
    // 和 NullVideoSink 保持同一套契约：没打开 = 没有可绘制区域
    return open_ ? renderTarget_.get() : nullptr;
}

core::Size SdlVideoSink::RenderSize() const
{
    return window_ ? window_->ClientSize() : core::Size{ 0, 0 };
}

void SdlVideoSink::SetTitle(std::string_view title)
{
    if (window_) window_->SetTitle(std::string(title));
}

} // namespace av::output::sdl
