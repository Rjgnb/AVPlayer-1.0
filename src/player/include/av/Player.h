#pragma once

// ===========================================================================
// avplayer 的对外门面（Facade）
//
// 设计要点（也是这份代码最值得学的地方）：
//   1) **本头文件不含任何 FFmpeg / SDL 类型** —— 实现全在 Player.cpp 里（pimpl）。
//      于是 app/Qt 想"顺手调一下 FFmpeg"会直接编译不过：依赖方向被编译器守住。
//   2) **Player 不自带主循环**：宿主每帧调一次 Tick()。
//      控制台里是 while + sleep，Qt 里就是一个 QTimer —— 同一套核心两种宿主。
//   3) 所有 public 方法都要求**在同一个（主）线程调用**，回调也从 Tick() 内发出。
//      这条约定让 Qt 侧不需要任何跨线程 marshal，信号槽天然安全。
// ===========================================================================
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "av/core/InputEvent.h"
#include "av/core/PlayerConfig.h"
#include "av/core/Status.h"
#include "av/core/Types.h"
#include "av/output/Backend.h"

namespace av {

enum class PlayerState
{
    Idle,      // 没有会话
    Opening,   // 正在打开（阻塞中）
    Ready,     // 已打开、已探测，还没开始播放
    Playing,
    Paused,
    Seeking,   // 保留：seek 是异步的，需要"正在跳转"的中间态时使用
    Ended,
    Error,
};

const char* ToString(PlayerState state) noexcept;

// 媒体描述：把 media 层的信息"翻译"成不含第三方类型的中性结构
struct MediaDescription
{
    std::string url;
    std::string container;
    std::string codecSummary;      // 例如 "h264 + aac"

    double durationSeconds = 0.0;
    bool   hasAudio = false;
    bool   hasVideo = false;

    core::AudioFormat audioFormat;   // 预期格式（以首帧实测为准）
    core::VideoFormat videoFormat;
    double            frameRate = 0.0;

    bool HasDuration() const noexcept { return durationSeconds > 0.0; }
};

// 运行时统计（诊断 A/V 偏差、丢帧；测试里也可以直接断言）
struct PlayerStats
{
    std::uint64_t presentedFrames = 0;
    std::uint64_t droppedFrames   = 0;
    double        lastVideoPts    = -1.0;
};

// 观察者：在 Qt 里就是"发信号"的那一层
class PlayerObserver
{
public:
    virtual ~PlayerObserver() = default;

    virtual void OnStateChanged(PlayerState state) { (void)state; }
    virtual void OnMediaOpened(const MediaDescription& media) { (void)media; }
    virtual void OnPositionChanged(double seconds) { (void)seconds; }
    virtual void OnDurationChanged(double seconds) { (void)seconds; }
    virtual void OnError(const core::Status& status) { (void)status; }
    virtual void OnEnded() {}
    // 诊断用：每呈现一帧视频回调一次（A/V 偏差分析）
    virtual void OnFramePresented(double ptsSeconds) { (void)ptsSeconds; }
    // 输入事件（主线程）：UI 层在此做 键位 -> 动作 的映射
    virtual void OnInputEvent(const core::InputEvent& event) { (void)event; }
};

class Player
{
public:
    // backend 由调用方提供（app 决定"用哪个后端"）；nullptr 会导致 Play() 失败
    explicit Player(std::shared_ptr<output::IBackend> backend, const core::PlayerConfig& config = {});
    ~Player();

    Player(const Player&)            = delete;
    Player& operator=(const Player&) = delete;

    // ---- 生命周期 ----
    core::Status Open(const std::string& url);   // 打开并探测（不开线程、不建窗口）
    void         Close();                        // 停止一切并释放资源；可重复调用
    bool         IsOpen() const;

    // ---- 播放控制 ----
    core::Status Play();                  // 开始/恢复；可重复调用
    void         Pause();
    void         TogglePlayPause();
    void         SetSpeed(double speed);  // 限幅在 [0.25, 4.0]
    void         SeekTo(double seconds);  // 异步：下一帧生效
    void         RequestQuit();           // 请求宿主退出（如收到关窗/CMake）

    // ---- 状态查询（主线程）----
    PlayerState             State() const;
    double                  Position() const;
    double                  Duration() const;
    double                  Speed() const;
    bool                    IsPlaying() const;
    bool                    QuitRequested() const;
    const MediaDescription& Media() const;
    PlayerStats             Stats() const;
    // 当前视频输出的可绘制区尺寸（像素）；没有视频输出时 {0,0}。
    // 覆盖层/进度条的几何必须按它算 —— 高分屏缩放时"像素"和"窗口坐标"不是一回事。
    core::Size              RenderSize() const;

    // ---- 宿主接口 ----
    void SetObserver(PlayerObserver* observer);
    // 覆盖层绘制钩子：在"视频帧已画、还没提交"之间被调用（见 IVideoSink 的 Draw/Present 约定）
    void SetOverlayPainter(std::function<void(output::IRenderTarget&)> painter);

    void Tick();                                        // 主线程每帧调用
    void HandleInputEvent(const core::InputEvent& event);   // 也可由宿主直接投喂（Qt 事件）

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace av
