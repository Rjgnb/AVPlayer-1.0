#include "SdlAudioSink.h"

#include "SdlInternal.h"

#include "av/core/Log.h"

namespace av::output::sdl {

namespace {
constexpr const char* kTag = "sdl-audio";
}

SdlAudioSink::~SdlAudioSink()
{
    Close();
}

core::AudioFormat SdlAudioSink::NegotiateFormat(const core::AudioFormat& source) const
{
    core::AudioFormat out = source;
    if (out.sampleRate <= 0) out.sampleRate = 48000;
    if (out.channels   <= 0) out.channels   = 2;

    switch (out.format)
    {
    case core::SampleFormat::U8:
    case core::SampleFormat::S16:
    case core::SampleFormat::S32:
    case core::SampleFormat::F32:
        break;   // SDL 原生支持，保持源格式（省掉一次格式转换）
    default:
        out.format = core::SampleFormat::S16;
        break;
    }
    return out;
}

core::Status SdlAudioSink::Open(const core::AudioFormat& format, const AudioSinkConfig& config)
{
    if (!format.IsValid())
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "音频格式无效");
    }
    if (config.bufferSamples <= 0 || config.bufferSamples > 65535)
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "bufferSamples 超出范围");
    }

    Close();   // 先关旧设备（Close 自己加锁，不能和下面的锁嵌套）

    std::lock_guard<std::mutex> lock(mutex_);

    SDL_AudioSpec desired{};
    desired.freq     = format.sampleRate;
    desired.format   = ToSdlAudioFormat(format.format);
    desired.channels = static_cast<Uint8>(format.channels);
    desired.samples  = static_cast<Uint16>(config.bufferSamples);
    desired.callback = nullptr;   // SDL_QueueAudio 模式

    SDL_AudioSpec obtained{};
    const char*   deviceName = config.device.empty() ? nullptr : config.device.c_str();

    // allowed_changes = 0：不许 SDL 偷偷改格式 —— 改了，音频主时钟的换算就是错的
    device_ = SDL_OpenAudioDevice(deviceName, 0, &desired, &obtained, 0);
    if (device_ == 0)
    {
        return core::Status::Error(core::StatusCode::Backend,
                                   std::string("SDL_OpenAudioDevice 失败: ") + SDL_GetError());
    }

    if (obtained.freq != desired.freq || obtained.channels != desired.channels ||
        obtained.format != desired.format || obtained.samples != desired.samples)
    {
        CloseLocked();
        return core::Status::Error(core::StatusCode::Backend,
                                   "SDL 返回了与请求不一致的音频规格（时钟换算会失准）");
    }

    format_     = format;
    deviceName_ = deviceName != nullptr ? deviceName : std::string();
    paused_     = true;
    SDL_PauseAudioDevice(device_, 1);   // 先暂停：等 Player 进入 Playing 再放行

    AV_LOG(core::LogLevel::Info, kTag)
        << "音频设备 " << format.sampleRate << "Hz/" << format.channels << "ch/"
        << core::ToString(format.format) << " 缓冲=" << config.bufferSamples << " 样本"
        << (deviceName_ .empty() ? std::string() : " [" + deviceName_ + "]");
    return core::Status::Ok();
}

void SdlAudioSink::CloseLocked()
{
    if (device_ != 0)
    {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    paused_ = true;
}

void SdlAudioSink::Close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    CloseLocked();
}

bool SdlAudioSink::IsOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return device_ != 0;
}

core::Status SdlAudioSink::Write(const core::AudioFrameView& pcm)
{
    if (pcm.data[0] == nullptr || pcm.nbSamples <= 0) return core::Status::Ok();

    std::lock_guard<std::mutex> lock(mutex_);
    if (device_ == 0)
    {
        return core::Status::Error(core::StatusCode::Backend, "音频设备未打开");
    }

    const std::size_t bytes = static_cast<std::size_t>(pcm.nbSamples) *
                              static_cast<std::size_t>(pcm.format.channels) *
                              static_cast<std::size_t>(core::BytesPerSample(pcm.format.format));
    if (SDL_QueueAudio(device_, pcm.data[0], static_cast<Uint32>(bytes)) != 0)
    {
        return core::Status::Error(core::StatusCode::Backend,
                                   std::string("SDL_QueueAudio 失败: ") + SDL_GetError());
    }
    return core::Status::Ok();
}

std::size_t SdlAudioSink::QueuedBytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (device_ == 0) return 0;
    return static_cast<std::size_t>(SDL_GetQueuedAudioSize(device_));
}

void SdlAudioSink::Pause(bool paused)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (device_ == 0 || paused_ == paused) return;
    paused_ = paused;
    SDL_PauseAudioDevice(device_, paused ? 1 : 0);
}

void SdlAudioSink::Flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (device_ == 0) return;
    SDL_ClearQueuedAudio(device_);
}

} // namespace av::output::sdl