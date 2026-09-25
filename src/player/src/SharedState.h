#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "av/core/BoundedQueue.h"
#include "av/core/MediaClock.h"
#include "av/core/PlayerConfig.h"
#include "av/core/Status.h"
#include "av/core/Tagged.h"
#include "av/media/AudioResampler.h"
#include "av/media/Decoder.h"
#include "av/media/Demuxer.h"
#include "av/media/Frame.h"
#include "av/media/Packet.h"
#include "av/media/VideoConverter.h"
#include "av/output/Backend.h"

namespace av::detail {

// 一次播放会话的共享状态。
//
// 为什么要有这个结构？三条后台线程 + 主线程需要看到同一份数据：
//   * 队列（生产者/消费者）
//   * 时钟（时间权威）
//   * 命令位（暂停/速度/seek/退出）
// 三件东西都放在这里，线程之间**只通过这个结构通信**，不互相持有指针 ——
// 这就是"线程间也解耦"：拆线程/合线程只需要改这里和 Player.cpp。
struct SharedState
{
    SharedState(std::shared_ptr<output::IBackend> backend, const core::PlayerConfig& config);

    // ---- 不可变（构造后不再改）----
    std::shared_ptr<output::IBackend> backend;
    core::PlayerConfig                config;

    // ---- 媒体（FFmpeg 侧）----
    media::Demuxer         demuxer;        // demux 线程
    media::MediaInfo       info;
    media::Decoder         audioDecoder;   // audio 线程
    media::Decoder         videoDecoder;   // video 线程
    media::AudioResampler  resampler;      // audio 线程
    media::VideoConverter  converter;      // 主线程（呈现时才转像素格式）

    // ---- 输出（后端侧）----
    std::unique_ptr<output::IAudioSink>   audioSink;   // audio 线程
    std::unique_ptr<output::IVideoSink>   videoSink;   // 主线程
    std::unique_ptr<output::IEventSource> events;      // 主线程

    // ---- 队列 ----
    // 注意：这里**不需要**给队列设 deleter —— 元素是 RAII 对象，析构即释放。
    // （P0 版本要 deleter，是因为队列里放的是裸 AVPacket*/AVFrame*）
    core::BoundedQueue<media::Packet>              audioPackets;
    core::BoundedQueue<media::Packet>              videoPackets;
    core::BoundedQueue<core::Tagged<media::Frame>> videoFrames;

    // ---- 时钟 ----
    core::MediaClock clock;

    // ---- 跨线程命令/状态 ----
    std::atomic<bool>   aborting{ false };
    std::atomic<bool>   paused{ true };
    std::atomic<bool>   quitRequested{ false };
    std::atomic<double> speed{ 1.0 };
    std::atomic<bool>   speedDirty{ false };      // 需要由 audio 线程重建重采样器
    std::atomic<double> seekRequest{ -1.0 };      // >= 0：demux 线程待执行的跳转
    std::atomic<double> seekTarget{ -1.0 };       // 供各 pipeline 丢弃目标之前的帧
    std::atomic<bool>   seekPreviewActive{ false }; // 暂停中 seek：允许短时丢弃目标前的音频包
    std::atomic<core::Generation> generation{ 0 };
    std::atomic<int>    runningThreads{ 0 };

    std::atomic<bool> audioEof{ false };
    std::atomic<bool> videoEof{ false };
    std::atomic<bool> demuxEof{ false };

    // ---- 位置/诊断（主线程读取）----
    std::atomic<double> lastVideoPts{ -1.0 };
    std::atomic<std::uint64_t> presentedFrames{ 0 };
    std::atomic<std::uint64_t> droppedFrames{ 0 };

    // ---- 门闸：暂停/恢复/中断的统一唤醒点 ----
    // 谓词都用原子量，notify 只是"加速"；即使丢一次唤醒，最坏也只是多等一个超时（10~50ms）。
    std::mutex              gate;
    std::condition_variable gateCv;

    void WakeAll() { gateCv.notify_all(); }

    template <class Predicate>
    bool WaitFor(Predicate predicate, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(gate);
        return gateCv.wait_for(lock, timeout, predicate);
    }

    // ---- 错误汇总（后台线程 -> 主线程）----
    std::mutex                errorMutex;
    std::vector<core::Status> pendingErrors;

    void                      PushError(const core::Status& status);
    std::vector<core::Status> TakeErrors();

    // ---- 便捷查询 ----
    bool HasAudio() const { return audioSink != nullptr; }
    bool HasVideo() const { return videoSink != nullptr; }
    std::size_t QueuedAudioBytes() const;
    std::size_t DiscardAudioBefore(double targetSeconds);
    core::Generation CurrentGeneration() const { return generation.load(); }
};

using SharedStatePtr = std::shared_ptr<SharedState>;

} // namespace av::detail
