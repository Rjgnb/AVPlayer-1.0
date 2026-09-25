#include "av/media/VideoConverter.h"

#include "av/core/Log.h"
#include "av/media/FFmpegUtil.h"
#include "av/media/Frame.h"

namespace av::media {

namespace {
constexpr const char* kTag = "convert";
}

struct VideoConverter::Impl
{
    SwsContext*        sws = nullptr;
    core::VideoFormat  input;
    core::PixelFormat  outputFormat = core::PixelFormat::Unknown;
    std::uint8_t*      planes[4]    = { nullptr, nullptr, nullptr, nullptr };
    int                strides[4]   = { 0, 0, 0, 0 };
    bool               configured   = false;

    ~Impl() { Release(); }

    void Release()
    {
        for (auto*& plane : planes)
        {
            av_freep(reinterpret_cast<void**>(&plane));
        }
        if (sws != nullptr) sws_freeContext(sws);
        sws        = nullptr;
        configured = false;
    }
};

VideoConverter::VideoConverter() : impl_(std::make_unique<Impl>()) {}
VideoConverter::~VideoConverter() = default;
VideoConverter::VideoConverter(VideoConverter&& other) noexcept : impl_(std::move(other.impl_)) {}
VideoConverter& VideoConverter::operator=(VideoConverter&& other) noexcept
{
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

core::Status VideoConverter::Configure(const core::VideoFormat& input, core::PixelFormat outputFormat)
{
    if (!input.IsValid() || outputFormat == core::PixelFormat::Unknown)
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "视频格式无效");
    }

    Impl& impl = *impl_;
    impl.Release();
    impl.input        = input;
    impl.outputFormat = outputFormat;

    if (outputFormat == input.format)
    {
        impl.configured = true;   // 直通，不需要 sws
        return core::Status::Ok();
    }

    const int size = av_image_alloc(impl.planes,
                                    impl.strides,
                                    input.width,
                                    input.height,
                                    static_cast<AVPixelFormat>(ToAVPixelFormat(outputFormat)),
                                    16);
    if (size < 0)
    {
        impl.Release();
        return FromAvError(size, "av_image_alloc");
    }

    impl.configured = true;
    AV_LOG(core::LogLevel::Debug, kTag)
        << "像素格式 " << core::ToString(input.format) << " -> " << core::ToString(outputFormat)
        << " (" << input.width << "x" << input.height << ")";
    return core::Status::Ok();
}

void VideoConverter::Close() { impl_->Release(); }

bool VideoConverter::IsConfigured() const noexcept { return impl_->configured; }

core::PixelFormat VideoConverter::OutputFormat() const noexcept { return impl_->outputFormat; }

core::Size VideoConverter::OutputSize() const noexcept
{
    return core::Size{ impl_->input.width, impl_->input.height };
}

core::Result<core::VideoFrameView> VideoConverter::Convert(const Frame& frame, double ptsSeconds, double durationSeconds)
{
    Impl& impl = *impl_;
    if (!impl.configured)
    {
        return core::Status::Error(core::StatusCode::Internal, "像素转换器未配置");
    }

    const AVFrame* source = frame.Raw();
    if (source == nullptr)
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "空视频帧");
    }

    if (impl.outputFormat == impl.input.format)
    {
        return MakeVideoView(frame, impl.input, durationSeconds);   // 直通：零拷贝
    }

    impl.sws = sws_getCachedContext(impl.sws,
                                    impl.input.width,
                                    impl.input.height,
                                    static_cast<AVPixelFormat>(ToAVPixelFormat(impl.input.format)),
                                    impl.input.width,
                                    impl.input.height,
                                    static_cast<AVPixelFormat>(ToAVPixelFormat(impl.outputFormat)),
                                    SWS_BILINEAR,
                                    nullptr,
                                    nullptr,
                                    nullptr);
    if (impl.sws == nullptr)
    {
        return core::Status::Error(core::StatusCode::Backend, "sws_getCachedContext 失败");
    }

    const int rows = sws_scale(impl.sws,
                               source->data,
                               source->linesize,
                               0,
                               impl.input.height,
                               impl.planes,
                               impl.strides);
    if (rows <= 0)
    {
        return core::Status::Error(core::StatusCode::Decode, "sws_scale 失败");
    }

    const int planeCount = impl.outputFormat == core::PixelFormat::Yuv420P ? 3
                         : impl.outputFormat == core::PixelFormat::Nv12    ? 2
                                                                          : 1;
    core::VideoFrameView view;
    for (int i = 0; i < planeCount; ++i)
    {
        view.planes[i]  = impl.planes[i];
        view.strides[i] = impl.strides[i];
    }
    view.format = core::VideoFormat{ impl.input.width, impl.input.height, impl.outputFormat, impl.input.frameRate };
    view.ptsSeconds      = ptsSeconds;
    view.durationSeconds = durationSeconds;
    return view;
}

} // namespace av::media
