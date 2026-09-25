#include "SharedState.h"

namespace av::detail {

SharedState::SharedState(std::shared_ptr<output::IBackend> backendRef, const core::PlayerConfig& playerConfig)
    : backend(std::move(backendRef)),
      config(playerConfig),
      audioPackets(playerConfig.audioPacketQueueCapacity),
      videoPackets(playerConfig.videoPacketQueueCapacity),
      videoFrames(playerConfig.videoFrameQueueCapacity)
{
}

void SharedState::PushError(const core::Status& status)
{
    if (status.ok()) return;
    std::lock_guard<std::mutex> lock(errorMutex);
    // 防雪崩：同一条错误最多留 8 条（解码错误可能每帧一条）
    if (pendingErrors.size() < 8) pendingErrors.push_back(status);
}

std::vector<core::Status> SharedState::TakeErrors()
{
    std::lock_guard<std::mutex> lock(errorMutex);
    std::vector<core::Status> out;
    out.swap(pendingErrors);
    return out;
}

std::size_t SharedState::QueuedAudioBytes() const
{
    return audioSink != nullptr ? audioSink->QueuedBytes() : 0;
}

std::size_t SharedState::DiscardAudioBefore(double targetSeconds)
{
    if (!info.hasAudio || targetSeconds < 0.0) return 0;

    const core::Rational timeBase = info.audio.timeBase;
    return audioPackets.DropFrontWhile([targetSeconds, timeBase](const media::Packet& packet) {
        const double pts = packet.PtsSeconds(timeBase);
        return pts >= 0.0 && pts + packet.DurationSeconds(timeBase) <= targetSeconds;
    });
}

} // namespace av::detail
