#include "av/output/NullBackend.h"

#include "av/core/Log.h"
#include "av/output/BackendRegistry.h"

namespace av::output {

namespace {
constexpr const char* kTag = "null";

// 文件作用域的兜底时钟。
// 不要写成函数内的 `static const SteadyClock`：那样会在"第一次调用者"的线程里做
// 延迟初始化，而第一次调用往往来自音频后台线程 —— 触发静态初始化的线程安全问题。
// 放到文件作用域，初始化发生在 main 之前，谁调用都安全。
const core::SteadyClock kFallbackClock;
}

// ---------------------------------------------------------------------------
// NullAudioSink：用墙钟模拟一块声卡
// ---------------------------------------------------------------------------
double NullAudioSink::NowSeconds() const
{
    return (clock_ != nullptr ? clock_ : &kFallbackClock)->NowSeconds();
}

void NullAudioSink::Drain() const
{
    const double now = NowSeconds();
    const double delta = now - lastMark_;
    lastMark_ = now;
    if (paused_ || delta <= 0.0 || byteRate_ <= 0.0) return;

    const auto played = static_cast<std::size_t>(delta * byteRate_);
    queuedBytes_ = played >= queuedBytes_ ? 0 : queuedBytes_ - played;
}

core::Status NullAudioSink::Open(const core::AudioFormat& format, const AudioSinkConfig&)
{
    if (!format.IsValid())
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "音频格式无效");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    format_    = format;
    byteRate_  = format.ByteRate();
    queuedBytes_  = 0;
    writtenBytes_ = 0;
    lastMark_  = NowSeconds();
    opened_    = true;
    paused_    = false;
    AV_LOG(core::LogLevel::Info, kTag)
        << "音频设备(模拟) " << format.sampleRate << "Hz/" << format.channels << "ch/"
        << core::ToString(format.format);
    return core::Status::Ok();
}

void NullAudioSink::Close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    opened_ = false;
    queuedBytes_ = 0;
}

core::Status NullAudioSink::Write(const core::AudioFrameView& pcm)
{
    if (!opened_)
    {
        return core::Status::Error(core::StatusCode::Backend, "NullAudioSink 未打开");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Drain();
    const auto bytes = static_cast<std::size_t>(pcm.nbSamples) *
                       static_cast<std::size_t>(pcm.format.channels) *
                       static_cast<std::size_t>(core::BytesPerSample(pcm.format.format));
    queuedBytes_ += bytes;
    writtenBytes_ += bytes;
    return core::Status::Ok();
}

std::size_t NullAudioSink::QueuedBytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    Drain();
    return queuedBytes_;
}

void NullAudioSink::Pause(bool paused)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Drain();
    paused_ = paused;
}

void NullAudioSink::Flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    Drain();
    queuedBytes_ = 0;
}

// ---------------------------------------------------------------------------
// NullVideoSink
// ---------------------------------------------------------------------------
core::Status NullVideoSink::Open(const core::VideoFormat& sourceFormat, const VideoSinkConfig& config)
{
    if (!sourceFormat.IsValid())
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "视频格式无效");
    }
    openedFormat_ = sourceFormat;
    openedSize_   = core::Size{ sourceFormat.width, sourceFormat.height };
    if (config.windowWidth > 0 && config.windowHeight > 0)
    {
        openedSize_ = core::Size{ config.windowWidth, config.windowHeight };
    }
    renderTarget_.SetSize(openedSize_);
    opened_      = true;
    frameCount_  = 0;
    lastPts_     = 0.0;
    AV_LOG(core::LogLevel::Info, kTag)
        << "视频输出(模拟) " << sourceFormat.width << "x" << sourceFormat.height
        << " " << core::ToString(sourceFormat.format);
    return core::Status::Ok();
}

void NullVideoSink::Close()
{
    opened_ = false;
}

core::Status NullVideoSink::Draw(const core::VideoFrameView& frame)
{
    if (!opened_)
    {
        return core::Status::Error(core::StatusCode::Backend, "NullVideoSink 未打开");
    }
    lastPts_ = frame.ptsSeconds;
    return core::Status::Ok();
}

void NullVideoSink::Present()
{
    ++frameCount_;
}

// ---------------------------------------------------------------------------
// NullEventSource / NullBackend
// ---------------------------------------------------------------------------
bool NullEventSource::Poll(core::InputEvent& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return false;
    out = events_.front();
    events_.pop_front();
    return true;
}

void NullEventSource::Push(core::InputEvent event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
}

std::unique_ptr<IAudioSink> NullBackend::CreateAudioSink()
{
    return std::make_unique<NullAudioSink>();
}

std::unique_ptr<IVideoSink> NullBackend::CreateVideoSink()
{
    return std::make_unique<NullVideoSink>();
}

std::unique_ptr<IEventSource> NullBackend::CreateEventSource()
{
    return std::make_unique<NullEventSource>();
}

void RegisterNullBackend(bool makeDefault)
{
    BackendRegistry::Instance().Register("null", [] { return std::make_unique<NullBackend>(); }, makeDefault);
}

} // namespace av::output
