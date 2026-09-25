#pragma once

#include <mutex>
#include <string>

#include <SDL.h>

#include "av/output/AudioSink.h"

namespace av::output::sdl {

// SDL 音频设备（SDL_QueueAudio 模式：回调在设备线程里做 memcpy，逻辑简单、可控）
class SdlAudioSink final : public IAudioSink
{
public:
    ~SdlAudioSink() override;

    const char* Name() const override { return "sdl-audio"; }

    core::AudioFormat NegotiateFormat(const core::AudioFormat& source) const override;

    core::Status Open(const core::AudioFormat& format, const AudioSinkConfig& config) override;
    void         Close() override;
    bool         IsOpen() const override;

    core::Status Write(const core::AudioFrameView& pcm) override;
    std::size_t  QueuedBytes() const override;

    void Pause(bool paused) override;
    void Flush() override;

private:
    // 需要在持锁状态下关闭设备时的内部版本（SDLK: std::mutex 不可重入）
    void CloseLocked();

    mutable std::mutex mutex_;
    SDL_AudioDeviceID  device_ = 0;
    core::AudioFormat  format_;
    std::string        deviceName_;
    bool               paused_ = true;
};

} // namespace av::output::sdl