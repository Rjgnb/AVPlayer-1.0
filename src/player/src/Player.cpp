#include "av/Player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <thread>
#include <vector>

#include "AudioPipeline.h"
#include "PacketPump.h"
#include "SharedState.h"
#include "VideoPipeline.h"

#include "av/core/Log.h"
#include "av/media/FFmpegUtil.h"
#include "av/media/Frame.h"

namespace av {

namespace {

constexpr const char* kTag = "player";

constexpr double kSpeedMin = 0.25;
constexpr double kSpeedMax = 4.0;
constexpr double kPositionReportInterval = 0.20;   // 位置上报节流（秒）
constexpr double kPtsTolerance = 0.005;            // 5ms：避免"永远差一帧"
constexpr std::chrono::milliseconds kTickSleep{ 2 };

MediaDescription Describe(const media::MediaInfo& info)
{
    MediaDescription description;
    description.url            = info.url;
    description.container      = info.containerFormat;
    description.codecSummary   = info.codecSummary;
    description.durationSeconds = info.durationSeconds;
    description.hasAudio       = info.hasAudio;
    description.hasVideo       = info.hasVideo;
    description.audioFormat    = info.audio.format;
    description.videoFormat    = info.video.format;
    description.frameRate      = info.video.frameRate;
    return description;
}

} // namespace

const char* ToString(PlayerState state) noexcept
{
    switch (state)
    {
    case PlayerState::Idle:    return "Idle";
    case PlayerState::Opening: return "Opening";
    case PlayerState::Ready:   return "Ready";
    case PlayerState::Playing: return "Playing";
    case PlayerState::Paused:  return "Paused";
    case PlayerState::Seeking: return "Seeking";
    case PlayerState::Ended:   return "Ended";
    case PlayerState::Error:   return "Error";
    }
    return "Unknown";
}

// ===========================================================================
// Impl：所有实现细节（FFmpeg/SDL/线程）都关在这里
// ===========================================================================
struct Player::Impl
{
    Impl(std::shared_ptr<output::IBackend> backendRef, const core::PlayerConfig& playerConfig)
        : backend(std::move(backendRef)), config(playerConfig)
    {
    }

    Player* owner = nullptr;

    std::shared_ptr<output::IBackend> backend;
    core::PlayerConfig                config;
    std::string                       url;

    detail::SharedStatePtr                  state;
    std::vector<std::thread>                threads;
    std::unique_ptr<detail::PacketPump>     pump;
    std::unique_ptr<detail::AudioPipeline>  audioPipeline;
    std::unique_ptr<detail::VideoPipeline>  videoPipeline;

    PlayerObserver*                             observer = nullptr;
    std::function<void(output::IRenderTarget&)> overlayPainter;

    PlayerState                state_ = PlayerState::Idle;
    MediaDescription           media;
    core::Tagged<media::Frame> pendingFrame;
    // 最后画过的一帧：暂停/静止时重绘覆盖层要用（否则界面会"冻住"）
    core::Tagged<media::Frame> lastFrame;
    bool                       needsRepaint = false;
    // 暂停中发生了跳转：抓一帧新位置的画面做预览（否则拖完进度条看不到跳到哪）
    bool                       pausedSeekPreview = false;
    // Open 之前设置的倍速先记在这里，StartSession 时落地
    double                     pendingSpeed = 1.0;
    double                     lastReportedPosition = -1.0;
    bool                       running = false;
    bool                       quit    = false;

    // ---- 小工具 ----
    void SetState(PlayerState next)
    {
        if (state_ == next) return;
        state_ = next;
        if (observer != nullptr) observer->OnStateChanged(next);
    }

    void NotifyError(const core::Status& status)
    {
        if (observer != nullptr) observer->OnError(status);
    }

    double PositionSeconds() const
    {
        if (!state) return 0.0;
        return state->clock.Now(state->QueuedAudioBytes());
    }

    // ---- 会话生命周期 ----
    core::Status StartSession();
    void         StopSession();

    // ---- 主线程每帧的三件事 ----
    void PresentDueFrame();
    void DrawFrame(media::Frame& frame, double clockNow, bool repaint = false);
    void ReportPosition();
    void CheckEnded();
};

core::Status Player::Impl::StartSession()
{
    detail::SharedState& st = *state;

    // 会话开始才把倍速落地：SetSpeed() 允许在 Open() 之前被调用
    st.speed = pendingSpeed;
    st.clock.SetSpeed(pendingSpeed);

    if (backend) st.events = backend->CreateEventSource();

    // ---------------- 音频 ----------------
    if (config.enableAudio && st.info.hasAudio && st.info.audio.format.IsValid())
    {
        st.audioSink = backend ? backend->CreateAudioSink() : nullptr;
        if (st.audioSink)
        {
            const core::AudioFormat target = st.audioSink->NegotiateFormat(st.info.audio.format);

            output::AudioSinkConfig sinkConfig;
            sinkConfig.bufferSamples    = config.audioBufferSamples;
            sinkConfig.watermarkSeconds = config.audioWatermarkSeconds;

            core::Status status = st.audioSink->Open(target, sinkConfig);
            if (status.ok()) status = st.audioDecoder.Open(st.info.audio.stream, 1);

            if (status.ok())
            {
                status = st.resampler.Configure(st.info.audio.format, target, st.speed.load());
            }

            if (!status.ok())
            {
                // 降级：没有声音也要能看画面（比"整场播放失败"友好得多）
                AV_LOG(core::LogLevel::Warn, kTag) << "音频输出不可用，降级为纯视频: " << status.ToString();
                st.PushError(status);
                if (st.audioSink) st.audioSink->Close();
                st.audioSink.reset();
            }
            else
            {
                st.clock.SetByteRate(target.ByteRate());
                st.clock.SetSource(core::MediaClock::Source::Audio);
            }
        }
    }

    // ---------------- 视频 ----------------
    if (config.enableVideo && st.info.hasVideo && st.info.video.format.IsValid())
    {
        st.videoSink = backend ? backend->CreateVideoSink() : nullptr;
        if (st.videoSink)
        {
            output::VideoSinkConfig sinkConfig;
            sinkConfig.title      = config.windowTitle.empty() ? st.info.url : config.windowTitle;
            sinkConfig.vsync      = config.vsync;
            sinkConfig.resizable  = config.resizable;

            core::Status status = st.videoSink->Open(st.info.video.format, sinkConfig);
            if (status.ok()) status = st.videoDecoder.Open(st.info.video.stream, 0);

            if (!status.ok())
            {
                AV_LOG(core::LogLevel::Warn, kTag) << "视频输出不可用，降级为纯音频: " << status.ToString();
                st.PushError(status);
                if (st.videoSink) st.videoSink->Close();
                st.videoSink.reset();
            }
        }
    }

    if (!st.HasAudio()) st.clock.SetSource(core::MediaClock::Source::Wall);

    // ---------------- 起线程 ----------------
    // std::thread 构造失败会抛 std::system_error；如果已经起了前一条线程，
    // 不能让它带着 this 继续运行。这里统一回收后把异常翻译成 Status。
    try
    {
        pump = std::make_unique<detail::PacketPump>(state);
        threads.emplace_back([this] { pump->Run(); });

        if (st.HasAudio())
        {
            audioPipeline = std::make_unique<detail::AudioPipeline>(state);
            threads.emplace_back([this] { audioPipeline->Run(); });
        }
        if (st.HasVideo())
        {
            videoPipeline = std::make_unique<detail::VideoPipeline>(state);
            threads.emplace_back([this] { videoPipeline->Run(); });
        }
    }
    catch (const std::exception& error)
    {
        StopSession();
        state->quitRequested = false;   // 启动失败不是用户请求退出
        return core::Status::Error(core::StatusCode::Internal,
                                   std::string("创建播放线程失败: ") + error.what());
    }

    AV_LOG(core::LogLevel::Info, kTag)
        << "开始播放: " << (st.HasVideo() ? "视频" : "") << (st.HasAudio() ? "+音频" : "")
        << " 线程数=" << threads.size();
    return core::Status::Ok();
}

void Player::Impl::StopSession()
{
    if (!state) return;

    state->aborting = true;
    state->quitRequested = true;
    // 先把队列"放弃掉"：阻塞在 Push 上的线程才能立刻醒来退出
    state->audioPackets.Abort();
    state->videoPackets.Abort();
    state->videoFrames.Abort();
    state->WakeAll();

    for (std::thread& thread : threads)
    {
        if (thread.joinable()) thread.join();
    }
    threads.clear();
    pump.reset();
    audioPipeline.reset();
    videoPipeline.reset();

    if (state->audioSink) state->audioSink->Close();
    if (state->videoSink) state->videoSink->Close();

    pendingFrame = {};
    lastFrame    = {};
    needsRepaint = false;
    running = false;
}

void Player::Impl::PresentDueFrame()
{
    detail::SharedState& st = *state;
    if (st.videoSink == nullptr || !st.videoSink->IsOpen()) return;

    const double clockNow = st.clock.Now(st.QueuedAudioBytes());

    // 暂停时不推进画面，但事件仍要能被看见：只要输入改变了覆盖层，
    // 就把最后一帧连覆盖层重画一次（拖进度条/悬停/自动隐藏都靠它）。
    if (st.paused.load())
    {
        // 暂停时也要把队列排空。
        //
        // 为什么必须无条件排空（这是"暂停时拖进度条画面不动"的真正原因）：
        // 暂停时解码线程仍在往里灌帧，而它只有 16 个槽位。主线程一旦停止取帧，
        // 队列立刻被占满，解码线程就卡在 PushWaiting 上，此后一帧也解不出来 ——
        // 于是"新世代的目标帧"永远排不进来，预览自然永远等不到。
        // 排空队列 = 给解码线程腾地方，它才能一路解到目标位置。
        bool previewedFrame = false;
        if (pausedSeekPreview)
        {
            const double target = st.seekTarget.load();
            core::Tagged<media::Frame> candidate;
            while (st.videoFrames.Take(candidate, std::chrono::milliseconds(0)) == core::TakeResult::Ok)
            {
                if (candidate.generation != st.CurrentGeneration() || !candidate.value.HasData()) continue;
                if (target >= 0.0 && candidate.value.PtsSeconds() + candidate.value.DurationSeconds() <= target)
                {
                    continue;
                }
                lastFrame.value   = std::move(candidate.value);
                previewedFrame    = true;
                needsRepaint      = true;
                pausedSeekPreview = false;
                st.seekPreviewActive = false;
                break;
            }
        }

        if (needsRepaint && lastFrame.value.HasData())
        {
            needsRepaint = false;
            DrawFrame(lastFrame.value, clockNow, /*repaint=*/true);
            // 预览帧换掉了画面，所以它算"屏幕上的最后一帧"；单纯的覆盖层重绘不算
            if (previewedFrame) st.lastVideoPts = lastFrame.value.PtsSeconds();
        }
        return;
    }

    // "收尾期"：demux 与两条解码线程都到结尾了。
    // 这时时钟可能已经不再前进 —— 音频流常常比视频短一点（本样例 5.013 vs 5.133），
    // 于是最后一帧的 pts 永远等不到，画面会卡住、状态也永远进不了 Ended。
    // 收尾期的正确做法：剩下的帧不再看时钟，立刻放完（放完即 Ended）。
    const bool draining = st.demuxEof.load() && (!st.HasAudio() || st.audioEof.load()) &&
                          (!st.HasVideo() || st.videoEof.load());

    // 取出下一帧（不阻塞）。世代号不匹配 = seek 之前的残留，直接丢。
    if (!pendingFrame.value.HasData())
    {
        core::Tagged<media::Frame> candidate;
        while (st.videoFrames.Take(candidate, std::chrono::milliseconds(0)) == core::TakeResult::Ok)
        {
            if (candidate.generation != st.CurrentGeneration()) continue;
            pendingFrame = std::move(candidate);
            break;
        }
    }
    if (!pendingFrame.value.HasData()) return;

    const double pts = pendingFrame.value.PtsSeconds();
    if (!draining && pts > clockNow + kPtsTolerance) return;    // 还没到时间，等下一帧 Tick

    if (!draining && clockNow - pts > st.config.maxVideoLeadSeconds)   // 落后太多：丢帧追上
    {
        AV_LOG(core::LogLevel::Debug, kTag) << "丢帧: pts=" << pts << " 时钟=" << clockNow;
        ++st.droppedFrames;
        pendingFrame = {};
        return;
    }

    AV_LOG(core::LogLevel::Debug, kTag) << "呈现: pts=" << pts << " 时钟=" << clockNow;
    DrawFrame(pendingFrame.value, clockNow);
    lastFrame.value = std::move(pendingFrame.value);   // 留一份供暂停时重绘
    needsRepaint = false;
    pendingFrame = {};
}

void Player::Impl::DrawFrame(media::Frame& frame, double clockNow, bool repaint)
{
    (void)clockNow;
    detail::SharedState& st = *state;

    const double pts      = frame.PtsSeconds();
    const double duration = frame.DurationSeconds();

    // 以"帧上的真实像素格式"为准（容器里写的不一定准）
    const core::VideoFormat sourceFormat = media::VideoFormatOf(frame);

    if (st.videoSink->SupportsFormat(sourceFormat.format))
    {
        const core::VideoFrameView view = media::MakeVideoView(frame, sourceFormat, duration);
        const core::Status status = st.videoSink->Draw(view);
        if (!status.ok()) { st.PushError(status); return; }
    }
    else
    {
        // 后端不支持源码格式 -> 用 sws 转成它偏好的格式（这一路只在少数后端会发生）
        const core::PixelFormat preferred = st.videoSink->PreferredFormat();
        if (!st.converter.IsConfigured() || st.converter.OutputFormat() != preferred)
        {
            const core::Status status = st.converter.Configure(sourceFormat, preferred);
            if (!status.ok()) { st.PushError(status); return; }
        }
        const core::Result<core::VideoFrameView> converted = st.converter.Convert(frame, pts, duration);
        if (!converted.ok()) { st.PushError(converted.status()); return; }
        const core::Status status = st.videoSink->Draw(converted.value());
        if (!status.ok()) { st.PushError(status); return; }
    }

    // 覆盖层：插在 Draw 与 Present 之间（这就是 IVideoSink 拆两步的原因）
    if (overlayPainter && st.videoSink->RenderTarget() != nullptr)
    {
        overlayPainter(*st.videoSink->RenderTarget());
    }

    st.videoSink->Present();

    if (repaint) return;   // 重绘只是刷新画面，不算"新呈现了一帧"
    st.lastVideoPts = pts;
    ++st.presentedFrames;
    if (observer != nullptr) observer->OnFramePresented(pts);
}

void Player::Impl::ReportPosition()
{
    if (!state || observer == nullptr) return;

    const double position = PositionSeconds();
    if (lastReportedPosition >= 0.0 && std::abs(position - lastReportedPosition) < kPositionReportInterval) return;
    lastReportedPosition = position;
    observer->OnPositionChanged(position);
}

void Player::Impl::CheckEnded()
{
    if (!state || state_ != PlayerState::Playing) return;
    if (state->paused.load()) return;

    const bool audioDone = !state->HasAudio() || state->audioEof.load();
    const bool videoDone = !state->HasVideo() ||
                           (state->videoEof.load() && state->videoFrames.IsEmpty() &&
                            !pendingFrame.value.HasData());
    if (!(state->demuxEof.load() && audioDone && videoDone)) return;

    AV_LOG(core::LogLevel::Info, kTag) << "播放结束";
    SetState(PlayerState::Ended);
    if (observer != nullptr) observer->OnEnded();

    if (config.loop && owner != nullptr)
    {
        // 循环 = 重新打开再播。简单、可预期；代价是一次文件重开（约几十毫秒）。
        // 真要"无缝循环"可以在 demux 线程里做 seek(0)，扩展点已经在那儿了。
        owner->Play();
    }
}

// ===========================================================================
// Player：门面
// ===========================================================================
Player::Player(std::shared_ptr<output::IBackend> backend, const core::PlayerConfig& config)
    : impl_(std::make_unique<Impl>(std::move(backend), config))
{
    impl_->owner = this;
}

Player::~Player()
{
    Close();
}

core::Status Player::Open(const std::string& url)
{
    Impl& self = *impl_;
    Close();

    if (!self.backend)
    {
        // 约定：Open() 只在成功时进入 Ready；任何失败都进入 Error 并通知观察者。
        // 这样宿主只盯 State()/OnError 就能发现所有打开失败，不必逐条检查返回值。
        const core::Status status = core::Status::Error(core::StatusCode::Internal, "没有可用的输出后端");
        self.SetState(PlayerState::Error);
        self.NotifyError(status);
        return status;
    }
    if (url.empty())
    {
        const core::Status status = core::Status::Error(core::StatusCode::InvalidArgument, "url 为空");
        self.SetState(PlayerState::Error);
        self.NotifyError(status);
        return status;
    }

    self.url   = url;
    self.state = std::make_shared<detail::SharedState>(self.backend, self.config);
    detail::SharedState& st = *self.state;

    // Open() 本身是同步的；如果宿主在打开网络流/慢设备时请求退出，
    // 中断回调要能让 avformat_open_input / find_stream_info 返回。
    std::weak_ptr<detail::SharedState> weakState = self.state;
    st.demuxer.SetInterruptCallback([weakState] {
        const detail::SharedStatePtr state = weakState.lock();
        return state == nullptr || state->quitRequested.load();
    });

    media::DemuxOptions options;
    options.enableAudio = self.config.enableAudio;
    options.enableVideo = self.config.enableVideo;

    self.SetState(PlayerState::Opening);
    const core::Status status = st.demuxer.Open(url, options);
    if (!status.ok())
    {
        self.state.reset();
        self.SetState(PlayerState::Error);
        self.NotifyError(status);
        return status;
    }

    st.info    = st.demuxer.Info();
    self.media = Describe(st.info);

    st.clock.Reset(self.config.startSeconds);
    if (self.config.startSeconds > 0.0)
    {
        // 告诉各 pipeline "目标之前的数据一律丢掉"。
        // 为什么需要：seek 只能定位到关键帧，解码器会从关键帧开始吐一段"预滚"数据，
        // 不设这个值的话，起播瞬间会快放一段画面（最多 maxVideoLeadSeconds）。
        st.seekTarget = self.config.startSeconds;
        const core::Status seekStatus = st.demuxer.SeekTo(self.config.startSeconds);
        if (!seekStatus.ok()) st.PushError(seekStatus);
    }

    self.lastReportedPosition = -1.0;
    self.SetState(PlayerState::Ready);
    if (self.observer != nullptr)
    {
        self.observer->OnMediaOpened(self.media);
        self.observer->OnDurationChanged(self.media.durationSeconds);
    }

    AV_LOG(core::LogLevel::Info, "player")
        << "已打开 " << url << " [" << self.media.codecSummary << "] 时长=" << self.media.durationSeconds << "s";
    return core::Status::Ok();
}

void Player::Close()
{
    Impl& self = *impl_;
    self.StopSession();
    if (self.state)
    {
        self.state->demuxer.Close();
        self.state.reset();
    }
    self.pendingFrame = {};
    self.running      = false;
    self.SetState(PlayerState::Idle);
}

bool Player::IsOpen() const
{
    return impl_->state != nullptr;
}

core::Status Player::Play()
{
    Impl& self = *impl_;
    if (!self.state)
    {
        return core::Status::Error(core::StatusCode::Internal, "没有打开的媒体（先调用 Open）");
    }

    if (self.state_ == PlayerState::Ended)
    {
        // 播完之后再点播放：从头重新开始
        const std::string url = self.url;
        const core::Status status = Open(url);
        if (!status.ok()) return status;
    }

    if (!self.running)
    {
        const core::Status status = self.StartSession();
        if (!status.ok())
        {
            self.SetState(PlayerState::Error);
            self.NotifyError(status);
            return status;
        }
        self.running = true;
    }

    self.pausedSeekPreview = false;   // 恢复播放：预览交给正常的呈现路径
    self.state->seekPreviewActive = false;
    self.state->paused = false;
    self.state->clock.Pause(false);
    if (self.state->audioSink) self.state->audioSink->Pause(false);
    self.state->WakeAll();

    self.SetState(PlayerState::Playing);
    return core::Status::Ok();
}

void Player::Pause()
{
    Impl& self = *impl_;
    if (!self.state || !self.running) return;
    if (self.state->paused.exchange(true)) return;

    self.state->clock.Pause(true);
    if (self.state->audioSink) self.state->audioSink->Pause(true);
    self.state->WakeAll();

    self.SetState(PlayerState::Paused);
}

void Player::TogglePlayPause()
{
    if (IsPlaying()) Pause();
    else             Play();
}

void Player::SetSpeed(double speed)
{
    Impl& self = *impl_;
    const double clamped = std::clamp(speed, kSpeedMin, kSpeedMax);
    self.pendingSpeed = clamped;   // 记下来：Open() 之前调用也能生效
    if (!self.state) return;
    if (std::abs(clamped - self.state->speed.load()) < 1e-6) return;

    self.state->speed = clamped;
    self.state->clock.SetSpeed(clamped);   // 时钟立刻按新倍速折算
    self.state->speedDirty = true;         // 重采样器由音频线程重建
    self.state->WakeAll();

    AV_LOG(core::LogLevel::Info, kTag) << "倍速 -> " << clamped;
}

void Player::SeekTo(double seconds)
{
    Impl& self = *impl_;
    if (!self.state) return;

    double target = seconds > 0.0 ? seconds : 0.0;
    const double duration = self.media.durationSeconds;
    if (duration > 0.0 && target > duration) target = duration;

    const bool paused = self.state->paused.load();

    // 顺序很重要：先公布目标与预览标志，再 +1 世代并唤醒 ——
    // 消费端看到新世代时，目标已经就绪，音频侧的背压策略也已生效。
    self.state->seekTarget = target;
    self.pausedSeekPreview = paused;
    self.state->seekPreviewActive = paused;
    self.state->generation.fetch_add(1);
    self.state->seekRequest = target;
    self.state->WakeAll();

    // 已经取到主线程、但尚未呈现的旧世代帧也必须作废；视频队列会被
    // PacketPump 在 demux 线程中 Reset，而 pendingFrame 只属于主线程。
    self.pendingFrame = {};

    self.lastReportedPosition = -1.0;
    if (self.observer != nullptr) self.observer->OnPositionChanged(target);
}

void Player::RequestQuit()
{
    Impl& self = *impl_;
    self.quit = true;
    if (self.state)
    {
        self.state->quitRequested = true;
        self.state->WakeAll();
    }
}

PlayerState Player::State() const { return impl_->state_; }

double Player::Position() const { return impl_->PositionSeconds(); }

double Player::Duration() const { return impl_->media.durationSeconds; }

double Player::Speed() const
{
    return impl_->state ? impl_->state->speed.load() : impl_->pendingSpeed;
}

bool Player::IsPlaying() const { return impl_->state_ == PlayerState::Playing; }

bool Player::QuitRequested() const
{
    return impl_->quit || (impl_->state && impl_->state->quitRequested.load());
}

const MediaDescription& Player::Media() const { return impl_->media; }

core::Size Player::RenderSize() const
{
    if (!impl_->state || !impl_->state->videoSink) return core::Size{ 0, 0 };
    return impl_->state->videoSink->RenderSize();
}

PlayerStats Player::Stats() const
{
    PlayerStats stats;
    if (impl_->state)
    {
        stats.presentedFrames = impl_->state->presentedFrames.load();
        stats.droppedFrames   = impl_->state->droppedFrames.load();
        stats.lastVideoPts    = impl_->state->lastVideoPts.load();
    }
    return stats;
}

void Player::SetObserver(PlayerObserver* observer) { impl_->observer = observer; }

void Player::SetOverlayPainter(std::function<void(output::IRenderTarget&)> painter)
{
    impl_->overlayPainter = std::move(painter);
}

void Player::HandleInputEvent(const core::InputEvent& event)
{
    Impl& self = *impl_;

    // 平台级事件（关窗）：播放器自己处理，因为它不依赖任何 UI 策略
    if (event.type == core::InputEventType::Quit)
    {
        RequestQuit();
    }

    // 任何输入都可能改变覆盖层的外观 => 请求重绘一次（暂停时靠它刷新界面）
    self.needsRepaint = true;
    if (self.observer != nullptr) self.observer->OnInputEvent(event);
}

void Player::Tick()
{
    Impl& self = *impl_;
    if (!self.state) return;

    detail::SharedState& st = *self.state;

    st.clock.TickWall();

    // 1) 泵事件（必须在主线程：Windows 跨线程读窗口消息会收不到）
    if (st.events)
    {
        core::InputEvent event;
        while (st.events->Poll(event)) HandleInputEvent(event);
        if (st.events->QuitRequested()) RequestQuit();
    }

    // 2) 把后台线程的错误搬到主线程上报
    for (const core::Status& error : st.TakeErrors()) self.NotifyError(error);

    // 3) 呈现到时间的视频帧（内含覆盖层绘制与 Present）
    self.PresentDueFrame();

    // 4) 位置上报（节流）
    self.ReportPosition();

    // 5) 结束判定
    self.CheckEnded();

    if (self.quit) st.quitRequested = true;
}

} // namespace av
