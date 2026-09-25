#include "PacketPump.h"

#include "av/core/Log.h"
#include "av/media/FFmpegUtil.h"

namespace av::detail {

namespace {
constexpr const char* kTag = "demux";
// 队列满时的等待上限：每 10ms 回来看一眼控制位（seek / 退出）
constexpr auto kPushSlice = std::chrono::milliseconds(10);
}

void PacketPump::Run()
{
    SharedState& state = *state_;
    ++state.runningThreads;

    // 中断回调：让 av_read_frame / avformat_open_input 能被打断
    state.demuxer.SetInterruptCallback([&state] {
        return state.aborting.load() || state.quitRequested.load() || state.seekRequest.load() >= 0.0;
    });

    while (!state.aborting.load() && !state.quitRequested.load())
    {
        if (state.seekRequest.load() >= 0.0)
        {
            HandleSeekRequest();
            continue;
        }

        // 每个包都用一个新的 Packet：Push 之后这个对象已经被移走（Raw() == nullptr），
        // 再拿它去 av_read_frame 会直接空指针崩在 FFmpeg 里。
        // RAII 类型就老老实实"一个包一个对象"，构造函数很便宜。
        media::Packet packet;
        core::Status   error;
        const media::Demuxer::ReadResult result = state.demuxer.Read(packet, &error);
        if (result == media::Demuxer::ReadResult::EndOfStream)
        {
            state.demuxEof = true;
            break;
        }
        if (result == media::Demuxer::ReadResult::Cancelled)
        {
            continue;   // 被 seek/退出打断：回到循环顶部重新判断
        }
        if (result == media::Demuxer::ReadResult::Error)
        {
            state.PushError(error);
            state.demuxEof = true;
            break;
        }

        // 按流分发。队列满时等一等 —— 这就是背压（解封装不会把内存吃光）。
        // 但等待必须有上限：暂停时消费者不再取数据，无限等会让 seek 永远排不上队。
        const int streamIndex = packet.StreamIndex();

        core::BoundedQueue<media::Packet>* queue = nullptr;
        if (state.demuxer.IsAudioStream(streamIndex))
        {
            queue = &state.audioPackets;
        }
        else if (state.demuxer.IsVideoStream(streamIndex))
        {
            queue = &state.videoPackets;
        }
        else
        {
            continue;   // 无关流（Read 一般已过滤，这里再兜一层）
        }

        bool pushed = false;
        while (!pushed)
        {
            if (queue->PushWaiting(packet, kPushSlice))
            {
                pushed = true;
            }
            else if (state.aborting.load() || state.quitRequested.load())
            {
                break;                                   // 收工
            }
            else if (state.seekRequest.load() >= 0.0)
            {
                break;                                   // 让位给 seek：丢掉这个包，回循环顶部去跳转
            }
            else if (state.seekPreviewActive.load() && queue == &state.audioPackets &&
                     state.DiscardAudioBefore(state.seekTarget.load()) != 0)
            {
                continue;                                // 暂停预览：丢掉目标前的音频包，解除背压
            }
            else if (queue->IsClosed() || queue->IsAborted())
            {
                break;                                   // 队列被关：收工
            }
            // 否则只是"暂时满了"：继续等
        }

        if (state.aborting.load() || state.quitRequested.load()) break;
        if (!pushed && (queue->IsClosed() || queue->IsAborted()))
        {
            AV_LOG(core::LogLevel::Warn, kTag) << "包队列被关闭，demux 线程收工";
            break;
        }
    }

    // 收尾：关闭队列，让消费者把剩下的数据取完（而不是丢帧）
    state.audioPackets.Close();
    state.videoPackets.Close();
    AV_LOG(core::LogLevel::Debug, kTag) << "demux 线程结束";
    state.WakeAll();
    --state.runningThreads;
}

void PacketPump::HandleSeekRequest()
{
    SharedState& state = *state_;

    // 先把请求取走：否则 av_seek_frame 自己会被中断回调打断
    const double target = state.seekRequest.exchange(-1.0);
    if (target < 0.0) return;

    AV_LOG(core::LogLevel::Info, kTag) << "执行跳转 -> " << target << "s";

    // 1) 丢掉旧世代的数据（Reset 让队列重新可用；元素是 RAII，析构即释放）
    state.audioPackets.Reset();
    state.videoPackets.Reset();
    state.videoFrames.Reset();

    // 2) 时钟归零到目标时间；EOF 标志清掉
    state.clock.Reset(target);
    state.clock.MarkEnded(false);
    state.audioEof = false;
    state.videoEof = false;
    state.demuxEof = false;

    // 3) 真正跳转（解码器的 avcodec_flush_buffers 由各 pipeline 在新世代里自己做，
    //    这样"谁持有解码器谁负责 flush"，不需要跨线程调用解码器）
    const core::Status status = state.demuxer.SeekTo(target);
    if (!status.ok())
    {
        if (status.systemCode() == AVERROR_EXIT)
        {
            return;   // 又被新的 seek 打断：让下一轮处理
        }
        state.PushError(status);
        return;
    }

    state.WakeAll();
}

} // namespace av::detail
