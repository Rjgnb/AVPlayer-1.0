#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

#include "av/core/Clock.h"
#include "av/output/Backend.h"

namespace av::output {

// ---------------------------------------------------------------------------
// headless（无窗口/无声卡）后端。
//
// 它的价值不是"占位"，而是**让整套播放逻辑可以在 CI 里自动跑**：
//   * NullAudioSink 按真实墙钟消耗排队字节 -> 音频主时钟、背压、变速全都能被验证
//   * NullVideoSink 记录画过的帧与 pts      -> 可以断言"跳转后第一帧是不是目标位置"
// 能这样测，正是"后端可空"这一设计的回报。
// ---------------------------------------------------------------------------
class NullAudioSink final : public IAudioSink
{
public:
    const char* Name() const override { return "null-audio"; }

    core::AudioFormat NegotiateFormat(const core::AudioFormat& source) const override { return source; }

    core::Status Open(const core::AudioFormat& format, const AudioSinkConfig& config) override;
    void         Close() override;
    bool         IsOpen() const override { return opened_; }

    core::Status Write(const core::AudioFrameView& pcm) override;
    std::size_t  QueuedBytes() const override;

    void Pause(bool paused) override;
    void Flush() override;

    // ---- 测试用 ----
    std::size_t WrittenBytes() const { std::lock_guard<std::mutex> lock(mutex_); return writtenBytes_; }
    void        SetClock(const core::IClock* clock) { clock_ = clock; }

private:
    double NowSeconds() const;
    // 按墙钟把"已经播出去"的字节从队列里扣掉（用真实时间模拟声卡）
    void   Drain() const;

    bool                       opened_ = false;
    core::AudioFormat          format_;
    double                     byteRate_ = 0.0;
    const core::IClock*        clock_    = nullptr;
    mutable std::mutex         mutex_;
    mutable std::size_t        queuedBytes_  = 0;
    mutable std::size_t        writtenBytes_ = 0;
    mutable double             lastMark_     = 0.0;   // 上次结算排队字节的墙钟
    bool                       paused_       = false;
};

class NullRenderTarget final : public IRenderTarget
{
public:
    explicit NullRenderTarget(core::Size size) : size_(size) {}
    void SetSize(core::Size size) { size_ = size; }

    core::Size Size() const override { return size_; }
    void FillRects(const core::Rect*, std::size_t count, core::Color) override { fillCalls_ += count; }
    void DrawLines(const core::Line*, std::size_t count, core::Color, int) override { lineCalls_ += count; }

    std::size_t FillCalls() const { return fillCalls_; }
    std::size_t LineCalls() const { return lineCalls_; }

private:
    core::Size  size_;
    std::size_t fillCalls_ = 0;
    std::size_t lineCalls_ = 0;
};

class NullVideoSink final : public IVideoSink
{
public:
    const char* Name() const override { return "null-video"; }

    bool              SupportsFormat(core::PixelFormat) const override { return true; }
    core::PixelFormat PreferredFormat() const override { return core::PixelFormat::Yuv420P; }

    core::Status Open(const core::VideoFormat& sourceFormat, const VideoSinkConfig& config) override;
    void         Close() override;
    bool         IsOpen() const override { return opened_; }

    core::Status Draw(const core::VideoFrameView& frame) override;
    void         Present() override;

    // 未 Open 时必须返回 nullptr：IVideoSink 的契约是"没有可绘制区域就返回空"，
    // 否则 Player 会把覆盖层画到一个并不存在的 1280x720 画布上（曾是真 bug）。
    IRenderTarget* RenderTarget() override { return opened_ ? &renderTarget_ : nullptr; }
    core::Size     RenderSize() const override
    {
        return opened_ ? openedSize_ : core::Size{ 0, 0 };
    }

    // ---- 测试用 ----
    std::size_t FrameCount() const { return frameCount_; }
    double      LastPtsSeconds() const { return lastPts_; }

private:
    bool                opened_ = false;
    core::VideoFormat   openedFormat_;
    core::Size          openedSize_{ 0, 0 };
    NullRenderTarget    renderTarget_{ core::Size{ 0, 0 } };
    std::size_t         frameCount_ = 0;
    double              lastPts_    = 0.0;
};

// 可脚本化的事件源：测试里 Push 事件，播放器像用真窗口一样 Poll 它
class NullEventSource final : public IEventSource
{
public:
    bool Poll(core::InputEvent& out) override;
    bool QuitRequested() const override { return quit_; }

    void Push(core::InputEvent event);
    void RequestQuit() { quit_ = true; }

private:
    std::mutex                mutex_;
    std::deque<core::InputEvent> events_;
    std::atomic<bool>         quit_{ false };
};

class NullBackend final : public IBackend
{
public:
    const char* Name() const override { return "null"; }

    core::Status Initialize(const core::KeyValues& options) override { (void)options; return core::Status::Ok(); }
    void         Shutdown() override {}

    std::unique_ptr<IAudioSink>   CreateAudioSink() override;
    std::unique_ptr<IVideoSink>   CreateVideoSink() override;
    std::unique_ptr<IEventSource> CreateEventSource() override;
};

// 注册到全局注册表（和 sdl::RegisterBackend() 同一套约定）。
// 两个使用场景：① 无人值守/无界面运行（--backend null）；
//              ② 单元测试与端到端测试 —— 于是 CI 里不需要窗口和声卡。
// 重复调用是安全的：同名注册即覆盖。
void RegisterNullBackend(bool makeDefault = false);

} // namespace av::output
