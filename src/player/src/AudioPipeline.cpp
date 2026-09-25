#include "AudioPipeline.h"

#include "av/core/Log.h"
#include "av/media/Frame.h"

namespace av::detail {

namespace {
constexpr const char* kTag = "audio";
constexpr auto kWaitSlice = std::chrono::milliseconds(10);
}

void AudioPipeline::Run()
{
    SharedState& state = *state_;
    ++state.runningThreads;

    media::Packet            packet;
    std::vector<media::Frame> frames;
    core::Generation         localGeneration = state.CurrentGeneration();
    double                   dropBefore      = state.seekTarget.load();
    bool                     drained         = false;

    while (!state.aborting.load() && !state.quitRequested.load())
    {
        // (1) 倍速变化：swr 的上下文只在音频线程里重建（避免跨线程改同一个 SwrContext）
        if (state.speedDirty.exchange(false))
        {
            const core::Status status = state.resampler.SetSpeed(state.speed.load());
            if (!status.ok()) state.PushError(status);
        }

        // (2) 世代变化（seek）：flush 解码器与设备，丢弃目标之前的数据
        const core::Generation generation = state.CurrentGeneration();
        if (generation != localGeneration)
        {
            localGeneration = generation;
            dropBefore      = state.seekTarget.load();
            drained         = false;
            state.audioDecoder.Flush();
            if (state.audioSink) state.audioSink->Flush();
            continue;
        }

        // (3) 暂停：设备已暂停、时钟自动冻结，这里只等恢复
        if (state.paused.load())
        {
            // 暂停预览会继续解视频帧，demux 线程也必须继续前进。若音频队列
            // 无人消费，64 个包后 demux 就会被背压卡住，视频预览永远到不了
            // seek 目标。这里只丢弃“目标之前”的音频包；目标及之后的包保留，
            // 恢复播放后无需重新 seek 也能从正确位置继续。
            if (state.seekPreviewActive.load() && state.audioSink != nullptr)
            {
                const double target = state.seekTarget.load();
                if (target >= 0.0 && state.info.hasAudio)
                {
                    const std::size_t dropped = state.DiscardAudioBefore(target);
                    if (dropped != 0) continue;
                }
            }

            state.WaitFor([&state] { return !state.paused.load() || state.aborting.load() || state.quitRequested.load(); },
                          kWaitSlice);
            continue;
        }

        // (4) 取包
        const core::TakeResult take = state.audioPackets.Take(packet, std::chrono::milliseconds(50));
        if (take == core::TakeResult::Timeout) continue;
        if (take == core::TakeResult::Closed)
        {
            if (!drained)
            {
                drained = true;
                frames.clear();
                const core::Status status = state.audioDecoder.Decode(nullptr, frames);
                if (!status.ok()) state.PushError(status);
                WriteFrames(frames, dropBefore);
            }
            break;
        }

        // (5) 解码
        frames.clear();
        const core::Status status = state.audioDecoder.Decode(&packet, frames);
        if (!status.ok())
        {
            // 个别包解码失败不该终止整场播放：记下来继续
            state.PushError(status);
            continue;
        }

        // (6) 重采样 + 输出 + 更新时钟
        WriteFrames(frames, dropBefore);
    }

    AV_LOG(core::LogLevel::Debug, kTag) << "音频线程结束";
    state.audioEof = true;
    state.WakeAll();
    --state.runningThreads;
}

bool AudioPipeline::WaitForRoom(double watermarkBytes)
{
    SharedState& state = *state_;
    while (!state.aborting.load() && !state.quitRequested.load())
    {
        const bool paused = state.paused.load();
        const bool room   = state.audioSink != nullptr &&
                          static_cast<double>(state.audioSink->QueuedBytes()) < watermarkBytes;
        if (!paused && room) return true;

        state.WaitFor([&state, watermarkBytes] {
            return state.aborting.load() || state.quitRequested.load() ||
                   (!state.paused.load() && state.audioSink != nullptr &&
                    static_cast<double>(state.audioSink->QueuedBytes()) < watermarkBytes);
        }, kWaitSlice);
    }
    return false;
}

void AudioPipeline::WriteFrames(std::vector<media::Frame>& frames, double& dropBefore)
{
    SharedState& state = *state_;
    if (state.audioSink == nullptr || !state.audioSink->IsOpen()) return;

    // 水位阈值是"媒体秒"，内部会按倍速换算成字节
    const double watermarkBytes = state.clock.WatermarkBytes(state.config.audioWatermarkSeconds);

    for (media::Frame& frame : frames)
    {
        if (!frame.HasData()) continue;
        if (state.aborting.load() || state.quitRequested.load()) return;

        const double pts      = frame.PtsSeconds();
        const double duration = frame.DurationSeconds();

        // seek 之后、目标之前的数据一律丢掉（解码器会从关键帧开始吐）
        if (dropBefore >= 0.0 && pts + duration <= dropBefore) continue;
        if (dropBefore >= 0.0) dropBefore = -1.0;

        // 健壮性：极少数流会在中途换采样格式/采样率，
        // 此时按"帧上的真实格式"重建重采样器（输出格式不变）
        const core::AudioFormat frameFormat = media::AudioFormatOf(frame);
        if (frameFormat.IsValid() && frameFormat != state.resampler.InputFormat())
        {
            AV_LOG(core::LogLevel::Warn, kTag)
                << "音频格式变化: " << state.resampler.InputFormat().sampleRate << "Hz/"
                << state.resampler.InputFormat().channels << "ch -> "
                << frameFormat.sampleRate << "Hz/" << frameFormat.channels << "ch，重建重采样器";
            const core::Status reconfigure = state.resampler.Configure(frameFormat,
                                                                      state.resampler.OutputFormat(),
                                                                      state.speed.load());
            if (!reconfigure.ok()) { state.PushError(reconfigure); continue; }
        }

        const core::Result<core::AudioFrameView> view = state.resampler.Convert(frame, pts);
        if (!view.ok())
        {
            state.PushError(view.status());
            continue;
        }

        if (!WaitForRoom(watermarkBytes)) return;

        const core::Status status = state.audioSink->Write(view.value());
        if (!status.ok())
        {
            state.PushError(status);
            continue;
        }

        // 时钟的唯一输入：这一帧的媒体时间（设备排队字节由时钟自己折算）
        state.clock.NotifyAudioWritten(pts);
    }
}

} // namespace av::detail
