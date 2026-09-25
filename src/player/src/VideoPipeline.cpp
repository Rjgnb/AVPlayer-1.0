#include "VideoPipeline.h"

#include "av/core/Log.h"
#include "av/media/Frame.h"

namespace av::detail {

namespace {
constexpr const char* kTag = "video";
// 队列满时的等待上限：每 10ms 回来看一眼世代/退出（见 BoundedQueue::PushWaiting 的注释）
constexpr auto kPushSlice = std::chrono::milliseconds(10);
}

void VideoPipeline::Run()
{
    SharedState& state = *state_;
    ++state.runningThreads;


    media::Packet             packet;
    std::vector<media::Frame> frames;
    core::Generation          localGeneration = state.CurrentGeneration();
    double                    dropBefore      = state.seekTarget.load();
    bool                      drained         = false;

    while (!state.aborting.load() && !state.quitRequested.load())
    {
        const core::Generation generation = state.CurrentGeneration();
        if (generation != localGeneration)
        {
            localGeneration = generation;
            dropBefore      = state.seekTarget.load();
            drained         = false;
            state.videoDecoder.Flush();
            continue;
        }

        const core::TakeResult take = state.videoPackets.Take(packet, std::chrono::milliseconds(50));
        if (take == core::TakeResult::Timeout) continue;
        if (take == core::TakeResult::Closed)
        {
            if (!drained)
            {
                drained = true;
                frames.clear();
                const core::Status status = state.videoDecoder.Decode(nullptr, frames);
                if (!status.ok()) state.PushError(status);
                PushFrames(frames, localGeneration, dropBefore);
            }
            break;
        }

        frames.clear();
        const core::Status status = state.videoDecoder.Decode(&packet, frames);
        if (!status.ok())
        {
            state.PushError(status);
            continue;
        }
        PushFrames(frames, localGeneration, dropBefore);
    }

    AV_LOG(core::LogLevel::Debug, kTag) << "视频解码线程结束";
    state.videoEof = true;
    state.WakeAll();
    --state.runningThreads;
}

void VideoPipeline::PushFrames(std::vector<media::Frame>& frames, core::Generation generation, double& dropBefore)
{
    SharedState& state = *state_;

    for (media::Frame& frame : frames)
    {
        // Decode() 可能一次吐出一整批 B 帧。批次开始时世代没变，不代表
        // 处理到第 N 帧时寻址请求还没发生；因此必须逐帧设置世代栅栏，
        // 否则旧世代的帧会穿过 Reset 后的新队列，反复被主线程当作“预览候选”排掉。
        if (state.CurrentGeneration() != generation) return;
        if (!frame.HasData()) continue;

        const double pts      = frame.PtsSeconds();
        const double duration = frame.DurationSeconds();

        if (dropBefore >= 0.0 && pts + duration <= dropBefore)
        {
            continue;
        }
        if (dropBefore >= 0.0) dropBefore = -1.0;

        core::Tagged<media::Frame> tagged;
        tagged.value      = std::move(frame);
        tagged.generation = generation;

        // 队列满 -> 阻塞（背压）；被 Abort/Reset 打断时返回 false
        // 但等待必须有上限：暂停时主线程不再取帧，无限等会让 seek 永远排不上队（死锁）
        //
        // epoch 用来拦住"穿越 Reset 的过期元素"：Reset 之前构造好的 tagged 不能
        // 在 Reset 之后混进队列（否则主线程收到的全是旧位置的帧）。
        while (!state.videoFrames.PushWaiting(tagged, kPushSlice))
        {
            if (state.aborting.load() || state.quitRequested.load()) return;
            if (state.CurrentGeneration() != generation) return;   // 世代变了：这批帧不要再灌了
            if (state.videoFrames.IsClosed() || state.videoFrames.IsAborted()) return;
            // 否则只是"暂时满了"：继续等
        }
        if (state.aborting.load() || state.quitRequested.load()) return;
    }
}

} // namespace av::detail
