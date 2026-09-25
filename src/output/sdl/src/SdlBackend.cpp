#include "av/output/sdl/SdlBackend.h"

#include "SdlInternal.h"

#include "av/core/Log.h"
#include "av/output/BackendRegistry.h"
#include "SdlAudioSink.h"
#include "SdlEventSource.h"
#include "SdlVideoSink.h"

namespace av::output::sdl {

namespace {
constexpr const char* kTag = "sdl";
std::mutex g_sdlMutex;
int        g_sdlRefCount = 0;
} // namespace

core::Status AcquireSdl()
{
    std::lock_guard<std::mutex> lock(g_sdlMutex);
    if (g_sdlRefCount++ > 0) return core::Status::Ok();

    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
#if defined(_WIN32)
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        --g_sdlRefCount;
        return core::Status::Error(core::StatusCode::Backend,
                                   std::string("SDL_InitSubSystem 失败: ") + SDL_GetError());
    }

    SDL_version version{};
    SDL_GetVersion(&version);
    AV_LOG(core::LogLevel::Info, kTag)
        << "SDL " << static_cast<int>(version.major) << "." << static_cast<int>(version.minor)
        << "." << static_cast<int>(version.patch) << " 已初始化";
    return core::Status::Ok();
}

void ReleaseSdl()
{
    std::lock_guard<std::mutex> lock(g_sdlMutex);
    if (g_sdlRefCount == 0) return;
    if (--g_sdlRefCount == 0)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        AV_LOG(core::LogLevel::Debug, kTag) << "SDL 子系统已释放";
    }
}

SDL_AudioFormat ToSdlAudioFormat(core::SampleFormat format)
{
    switch (format)
    {
    case core::SampleFormat::U8:  return AUDIO_U8;
    case core::SampleFormat::S16: return AUDIO_S16SYS;
    case core::SampleFormat::S32: return AUDIO_S32SYS;
    case core::SampleFormat::F32: return AUDIO_F32SYS;
    case core::SampleFormat::Unknown: break;
    }
    return AUDIO_S16SYS;
}

Uint32 ToSdlPixelFormat(core::PixelFormat format)
{
    switch (format)
    {
    case core::PixelFormat::Yuv420P: return SDL_PIXELFORMAT_IYUV;
    case core::PixelFormat::Nv12:    return SDL_PIXELFORMAT_NV12;
    case core::PixelFormat::Bgra:    return SDL_PIXELFORMAT_ARGB8888;   // 小端下 = BGRA 字节序
    case core::PixelFormat::Rgba:    return SDL_PIXELFORMAT_ABGR8888;
    case core::PixelFormat::Unknown: break;
    }
    return SDL_PIXELFORMAT_UNKNOWN;
}

// ---------------------------------------------------------------------------
// SdlWindow
// ---------------------------------------------------------------------------
SdlWindow::~SdlWindow()
{
    Destroy();
}

core::Status SdlWindow::Create(const core::VideoFormat& format, const VideoSinkConfig& config)
{
    Destroy();

    const int width  = config.windowWidth  > 0 ? config.windowWidth  : format.width;
    const int height = config.windowHeight > 0 ? config.windowHeight : format.height;
    if (width <= 0 || height <= 0)
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "窗口尺寸无效");
    }

    Uint32 flags = SDL_WINDOW_SHOWN;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config.highDpi)   flags |= SDL_WINDOW_ALLOW_HIGHDPI;

    window_ = SDL_CreateWindow(config.title.empty() ? "avplayer" : config.title.c_str(),
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width, height, flags);
    if (window_ == nullptr)
    {
        return core::Status::Error(core::StatusCode::Backend,
                                   std::string("SDL_CreateWindow 失败: ") + SDL_GetError());
    }

    Uint32 rendererFlags = SDL_RENDERER_ACCELERATED;
    if (config.vsync) rendererFlags |= SDL_RENDERER_PRESENTVSYNC;

    renderer_ = SDL_CreateRenderer(window_, -1, rendererFlags);
    if (renderer_ == nullptr)
    {
        // 有些虚拟机/远程桌面没有硬件渲染器，退回软件渲染（健壮性）
        AV_LOG(core::LogLevel::Warn, kTag) << "硬件渲染器创建失败，改用软件渲染: " << SDL_GetError();
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer_ == nullptr)
    {
        Destroy();
        return core::Status::Error(core::StatusCode::Backend,
                                   std::string("SDL_CreateRenderer 失败: ") + SDL_GetError());
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    const core::Status textureStatus = EnsureTexture(format);
    if (!textureStatus.ok())
    {
        Destroy();
        return textureStatus;
    }

    AV_LOG(core::LogLevel::Info, kTag)
        << "窗口就绪 " << width << "x" << height << (config.resizable ? " (可缩放)" : "");
    return core::Status::Ok();
}

void SdlWindow::Destroy()
{
    if (texture_ != nullptr)  SDL_DestroyTexture(texture_);
    if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
    if (window_ != nullptr)   SDL_DestroyWindow(window_);
    texture_  = nullptr;
    renderer_ = nullptr;
    window_   = nullptr;
}

core::Size SdlWindow::ClientSize() const
{
    int width  = 0;
    int height = 0;
    // 高 DPI（SDL_WINDOW_ALLOW_HIGHDPI）下"渲染输出像素" > "窗口坐标点"。
    // 覆盖层几何与鼠标命中判定都以像素为单位，所以这里取渲染输出尺寸。
    if (renderer_ != nullptr &&
        SDL_GetRendererOutputSize(renderer_, &width, &height) == 0 &&
        width > 0 && height > 0)
    {
        return core::Size{ width, height };
    }
    if (window_ != nullptr) SDL_GetWindowSize(window_, &width, &height);
    return core::Size{ width, height };
}

core::Point SdlWindow::ToRenderPoint(core::Point windowPoint) const
{
    int windowWidth  = 0;
    int windowHeight = 0;
    if (window_ != nullptr) SDL_GetWindowSize(window_, &windowWidth, &windowHeight);

    const core::Size render = ClientSize();
    if (windowWidth <= 0 || windowHeight <= 0 || render.width <= 0 || render.height <= 0)
    {
        return windowPoint;   // 没有窗口/渲染器：按原样返回（缩放为 1）
    }

    const double scaleX = static_cast<double>(render.width)  / static_cast<double>(windowWidth);
    const double scaleY = static_cast<double>(render.height) / static_cast<double>(windowHeight);
    return core::Point{ static_cast<int>(windowPoint.x * scaleX + 0.5),
                        static_cast<int>(windowPoint.y * scaleY + 0.5) };
}

void SdlWindow::SetTitle(const std::string& title)
{
    if (window_ != nullptr && !title.empty()) SDL_SetWindowTitle(window_, title.c_str());
}

core::Status SdlWindow::EnsureTexture(const core::VideoFormat& format)
{
    if (texture_ != nullptr &&
        textureFormat_.format == format.format &&
        textureFormat_.width == format.width &&
        textureFormat_.height == format.height)
    {
        return core::Status::Ok();
    }

    if (texture_ != nullptr) SDL_DestroyTexture(texture_);
    texture_ = nullptr;

    const Uint32 sdlFormat = ToSdlPixelFormat(format.format);
    if (sdlFormat == SDL_PIXELFORMAT_UNKNOWN)
    {
        return core::Status::Error(core::StatusCode::Unsupported,
                                   std::string("SDL 不支持像素格式 ") + core::ToString(format.format));
    }

    // STREAMING 纹理：内容每帧更新（SDL 会尽量用显存直接写）
    texture_ = SDL_CreateTexture(renderer_, sdlFormat, SDL_TEXTUREACCESS_STREAMING,
                                 format.width, format.height);
    if (texture_ == nullptr)
    {
        return core::Status::Error(core::StatusCode::Backend,
                                   std::string("SDL_CreateTexture 失败: ") + SDL_GetError());
    }
    textureFormat_ = format;
    return core::Status::Ok();
}

SDL_Rect SdlWindow::DestinationRect() const
{
    const core::Size client = ClientSize();
    if (textureFormat_.width <= 0 || textureFormat_.height <= 0)
    {
        return SDL_Rect{ 0, 0, client.width, client.height };
    }

    const double scaleX = static_cast<double>(client.width)  / textureFormat_.width;
    const double scaleY = static_cast<double>(client.height) / textureFormat_.height;
    const double scale  = scaleX < scaleY ? scaleX : scaleY;

    const int width  = static_cast<int>(textureFormat_.width * scale + 0.5);
    const int height = static_cast<int>(textureFormat_.height * scale + 0.5);
    return SDL_Rect{ (client.width - width) / 2, (client.height - height) / 2, width, height };
}

void SdlWindow::Draw(const core::VideoFrameView& frame)
{
    if (!IsValid()) return;

    const core::Status textureStatus = EnsureTexture(frame.format);
    if (!textureStatus.ok())
    {
        AV_LOG(core::LogLevel::Error, kTag) << textureStatus.ToString();
        return;
    }

    switch (frame.format.format)
    {
    case core::PixelFormat::Yuv420P:
        SDL_UpdateYUVTexture(texture_, nullptr,
                             frame.planes[0], frame.strides[0],
                             frame.planes[1], frame.strides[1],
                             frame.planes[2], frame.strides[2]);
        break;
    case core::PixelFormat::Nv12:
        SDL_UpdateNVTexture(texture_, nullptr,
                            frame.planes[0], frame.strides[0],
                            frame.planes[1], frame.strides[1]);
        break;
    default:
        SDL_UpdateTexture(texture_, nullptr, frame.planes[0], frame.strides[0]);
        break;
    }

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    const SDL_Rect destination = DestinationRect();
    SDL_RenderCopy(renderer_, texture_, nullptr, &destination);
}

void SdlWindow::Present()
{
    if (renderer_ != nullptr) SDL_RenderPresent(renderer_);
}

// ---------------------------------------------------------------------------
// SdlContext
// ---------------------------------------------------------------------------
std::shared_ptr<SdlWindow> SdlContext::Window()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return window_;
}

std::shared_ptr<SdlWindow> SdlContext::EnsureWindow(const VideoSinkConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!window_) window_ = std::make_shared<SdlWindow>();
    return window_;
}

void SdlContext::ResetWindow()
{
    std::shared_ptr<SdlWindow> window;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        window = std::move(window_);
        window_.reset();
    }
    if (window) window->Destroy();
}

// ---------------------------------------------------------------------------
// SdlBackend
// ---------------------------------------------------------------------------
struct SdlBackend::Impl
{
    // shared_ptr：窗口由"视频 sink 创建、事件源也能看到"，
    // 用共享所有权避免"谁先析构"的悬垂引用问题
    std::shared_ptr<SdlContext> context      = std::make_shared<SdlContext>();
    bool                        initialized  = false;
};

SdlBackend::SdlBackend() : impl_(std::make_unique<Impl>()) {}

SdlBackend::~SdlBackend()
{
    Shutdown();
}

core::Status SdlBackend::Initialize(const core::KeyValues& options)
{
    if (impl_->initialized) return core::Status::Ok();

    const core::Status status = AcquireSdl();
    if (!status.ok()) return status;

    impl_->initialized = true;
    if (options.Contains("audio_device"))
    {
        AV_LOG(core::LogLevel::Debug, kTag) << "后端选项 audio_device=" << options.Get("audio_device");
    }
    return core::Status::Ok();
}

void SdlBackend::Shutdown()
{
    if (!impl_->initialized) return;
    impl_->context->ResetWindow();
    ReleaseSdl();
    impl_->initialized = false;
}

std::unique_ptr<IAudioSink> SdlBackend::CreateAudioSink()
{
    return std::make_unique<SdlAudioSink>();
}

std::unique_ptr<IVideoSink> SdlBackend::CreateVideoSink()
{
    return std::make_unique<SdlVideoSink>(impl_->context);
}

std::unique_ptr<IEventSource> SdlBackend::CreateEventSource()
{
    return std::make_unique<SdlEventSource>(impl_->context);
}

void RegisterBackend()
{
    BackendRegistry::Instance().Register("sdl", [] { return std::make_unique<SdlBackend>(); });
}

} // namespace av::output::sdl
