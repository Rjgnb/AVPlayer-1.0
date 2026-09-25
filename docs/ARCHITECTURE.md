# avplayer 架构设计（两轮审查后定稿）

> 目标读者：本项目作者（学习者）。本文只讲"为什么这么分层、边界在哪、接口长什么样"，实现细节在代码与 `docs/PLAN.md`。
> 状态：**第一轮有条件通过，第二轮实现复核通过。**  
> 审查记录：[REVIEW_ROUND1.md](REVIEW_ROUND1.md)、[REVIEW_ROUND2.md](REVIEW_ROUND2.md)。

---

## 0. 怎么读这份文档

1. 先看第 2 节的"问题 → 目标"对照表，确认我们对"为什么要重写"的理解一致；
2. 再看第 3 节的**分层与依赖规则**（这是全部设计的地基，只有 5 条规则）；
3. 然后看第 5 节**接口清单**（要审查的就是这些签名）；
4. 第 6/7 节是线程模型与时序，这是音视频播放器最容易出错的地方；第二轮的真实缺陷也集中在这里；
5. 第 11 节回答"以后换后端 / 换平台要怎么改"。

---

## 1. 设计目标与非目标

**目标**

| # | 目标 | 可验证的判据 |
|---|---|---|
| G1 | 播放核心与"具体平台/后端"解耦 | 换后端只新增 `src/output/<name>/`，`media/`、`player/`、`ui/` 零改动 |
| G2 | 功能可增量扩展（暂停/倍速/跳转/进度条） | 每个功能只落在 1~2 个模块，不跨层修改 |
| G3 | 逻辑清晰、可学习 | 每个模块能用一句话说清职责；依赖方向单向 |
| G4 | 工程化 | CMake 多目标、分层可被构建系统强制、有单元测试、有 ADR 式文档 |
| G5 | 为 Qt / Linux 铺路 | 控制逻辑不依赖阻塞主循环；对外只暴露观察者接口（可映射为信号槽） |
| G6 | 健壮性 | RAII 管理 FFmpeg/SDL 资源；错误用返回值传播而非崩溃；线程退出路径可重入 |

**非目标（明确不做，避免过度设计）**

- 不做音视频编辑、不做滤镜链编辑（atempo 只作为变速的可选策略）；
- 不做网络流协议适配（demux 走 FFmpeg，不自己实现 RTMP/HLS）；
- 不做多路同时播放（一次一个会话，但架构不阻碍以后加）；
- 不做硬件解码零拷贝（预留 `IVideoSink` 接收 dmabuf 的扩展点即可）。

---

## 2. 现状问题 → 目标形态

| 现状（AVPlay_1） | 的问题 | 目标形态 |
|---|---|---|
| `SDLPlay` 一个类管窗口+音频设备+重采样+时钟+同步+队列+线程 | 7 个关注点耦合；改任一功能都要动同一文件 | 拆成 `output/sdl`（设备/窗口）+ `player`（编排）+ `core`（时钟/队列）+ `ui`（覆盖层） |
| `AVPlay` 同时是门面、解封装、解码、线程管理 | 门面 API 与实现细节混在一起，Qt 侧无法复用 | `Player` 只做门面与命令；`media/` 独立承担解封装与解码 |
| 上层直接看到 `AVFrame*` / `AVStream*` | SDL 层被 FFmpeg 类型绑死，指针生命周期脆弱 | 跨层只传 `VideoFrameView` / `AudioFrameView`（纯数据视图） |
| 时钟公式散在 `SDLPlay` 里 | 倍速/跳转要到处补丁 | `MediaClock` 单点持有时间权威 |
| 队列只有 `close()`（不可逆） | seek 的 flush 做不了 | 队列有 `Close/Abort/Reset` 三种语义 + deleter |
| 没有世代号 | seek 后旧帧无法识别 | `Tagged<T>{ value, generation }` + `Generation` |
| 主循环与事件泵隐含在渲染线程里 | 窗口无响应、无法接 Qt | 主线程**显式**跑 `Tick()`（泵事件+呈现+命令），Qt 里用定时器驱动同一个 `Tick()` |
| 无测试、无构建系统 | 改动靠手测，跨平台迁移困难 | CMake 多目标 + 单元测试 + headless(null) 后端可自动验证 |

**一句话总结**：现有代码的阻碍不是"实现写得差"，而是**边界划错**——把"会变化的轴"（后端、UI、同步策略）和"稳定逻辑"（时序编排）压进了同一个类。

---

## 3. 分层与依赖规则（地基）

```
        ┌──────────────────────────────────────────────────────────┐
        │ app（可执行：控制台演示 / 未来的 Qt 窗口）                │
        └───────────────┬──────────────────────────────────────────┘
                        │ 只依赖 Player 与"具体后端"
        ┌───────────────▼───────────────┐   ┌──────────────────────┐
        │ player（编排：状态机/命令/时钟）│──▶│ ui（覆盖层：进度条等）│
        └───────┬───────────────┬───────┘   └──────────┬───────────┘
                │               │                      │
        ┌───────▼──────┐ ┌──────▼───────────────────────▼────────┐
        │ media        │ │ output（接口 + 具体后端实现）          │
        │ 解封装/解码/ │ │  IAudioSink / IVideoSink / IEventSource│
        │ 重采样       │ │  IRenderTarget                         │
        └───────┬──────┘ └──────┬────────────────────────────────┘
                │               │
        ┌───────▼───────────────▼───────┐
        │ core（无第三方依赖的基础设施）│
        │  Log / Status / Queue / 类型  │
        └───────────────────────────────┘
```

**依赖规则（只有 5 条，违反即架构腐化）**

1. 依赖只能向下（`app → player/ui → media/output → core`）；
2. `core` 不依赖任何人（不 include FFmpeg/SDL）；
3. `media` 可以用 FFmpeg，但**不允许** include SDL 或任何 UI 头；
4. `output` 的接口层不依赖 FFmpeg/SDL；**具体后端**（`output/sdl`）可以依赖 SDL，但只能依赖 `output` 的接口和 `core` 的类型；
5. `player`、`ui` **不允许**出现 `SDL_*` / `AVFrame` / `AVPacket` 等符号。

**这些规则不是靠自觉，而是靠构建目标强制**：每个层次是独立的 CMake target，谁 link 谁就决定了能否 include（第 4 节）。这是"工程化"最关键的一步——把架构约定变成编译期约束。

---

## 4. 目录与构建目标

```
avplayer/
  CMakeLists.txt            # 顶层：选项、子目录
  cmake/                    # 第三方查找、警告级别、DLL 拷贝
  docs/                     # 架构 / 计划 / 学习 / 后端
  src/
    core/     → target avcore        （无第三方依赖）
    media/    → target avmedia       （+ FFmpeg）
    output/   → target avoutput      （接口 + null 后端 + 注册表；无第三方依赖）
      sdl/    → target avoutput_sdl  （+ SDL2）
      null/   （编在 avoutput 里：headless/测试用）
    player/   → target avplayer      （+ avmedia + avoutput）
    ui/       → target avui          （+ avcore + avoutput 接口）
    app/console/ → target av_console （唯一决定"用哪个后端"的地方）
  tests/      → target av_tests
  tools/      # 辅助脚本（跑样例、生成统计）
```

分层可见性靠 `target_link_libraries` 的 `PUBLIC/PRIVATE` 控制：例如 `avplayer` 私有链接 `avmedia` 与 `avoutput`，于是 app 想做"直接调 FFmpeg"就必须显式加依赖——**摩擦即约束**。

---

## 5. 核心抽象（接口清单）

### 5.1 core：跨层数据契约（纯数据，无第三方）

```cpp
namespace av::core {

struct Rational { std::int64_t num = 1, den = 1; };          // 时基
double  ToSeconds(std::int64_t ticks, Rational tb);
std::int64_t FromSeconds(double sec, Rational tb);

struct Size  { int width = 0, height = 0; };
struct Point { int x = 0, y = 0; };
struct Rect  { int x = 0, y = 0, width = 0, height = 0; };
struct Color { std::uint8_t r = 0, g = 0, b = 0, a = 255; };

enum class PixelFormat { Unknown, Bgra, Rgba, Yuv420P, Nv12 }; // 只列我们用到的，可扩展
enum class SampleFormat { Unknown, U8, S16, S32, F32 };

struct AudioFormat { int sampleRate = 0; int channels = 0; SampleFormat format = SampleFormat::Unknown; };
struct VideoFormat { int width = 0, height = 0; PixelFormat format = PixelFormat::Unknown; Rational frameRate; };

// 只读视图：不拥有内存，生命周期由上层保证（详见 6.3 的所有权约定）
struct VideoFrameView {
    const std::uint8_t* planes[4] = { nullptr, nullptr, nullptr, nullptr };
    int                 strides[4] = { 0, 0, 0, 0 };
    VideoFormat         format;
    double              ptsSeconds = 0.0;
    double              durationSeconds = 0.0;
};
struct AudioFrameView {
    const std::uint8_t* data[8] = {};
    int                 nbSamples = 0;        // 每通道采样数
    AudioFormat         format;
    double              ptsSeconds = 0.0;
};
} // namespace av::core
```

> 关键设计：**视图（View）与所有者（Owner）分离**。`media` 层内部用 RAII 持有 `AVFrame`，往上只传视图；后端在 `Write/Present` 返回前必须完成消费（拷贝或提交给设备）。这一条决定了整套代码"指针不裸奔"。

### 5.2 output：后端接口（可替换的边界）

```cpp
namespace av::output {

struct AudioSinkConfig { std::string device; int bufferSamples = 1024; double watermarkSeconds = 0.3; bool allowResample = true; };
struct VideoSinkConfig { std::string title; bool vsync = true; bool resizable = true; bool highDpi = true; int windowWidth = 0, windowHeight = 0; };

class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    virtual const char* Name() const = 0;
    virtual core::AudioFormat NegotiateFormat(const core::AudioFormat& source) const = 0;
    virtual core::Status Open(const core::AudioFormat& format, const AudioSinkConfig& cfg) = 0;
    virtual void         Close() = 0;
    virtual bool         IsOpen() const = 0;
    virtual core::Status Write(const core::AudioFrameView& pcm) = 0;   // 返回前必须接纳完
    virtual std::size_t  QueuedBytes() const = 0;                      // 时钟用：还有多少字节没播出去
    virtual void         Pause(bool paused) = 0;
    virtual void         Flush() = 0;                                  // 跳转：丢掉已排队音频
};

class IVideoSink {
public:
    virtual ~IVideoSink() = default;
    virtual const char* Name() const = 0;
    virtual bool SupportsFormat(core::PixelFormat format) const = 0;
    virtual core::PixelFormat PreferredFormat() const = 0;
    virtual core::Status Open(const core::VideoFormat& format, const VideoSinkConfig& cfg) = 0;
    virtual void         Close() = 0;
    virtual bool         IsOpen() const = 0;
    virtual core::Status Draw(const core::VideoFrameView& frame) = 0;    // 后台缓冲
    virtual void         Present() = 0;                                  // 提交屏幕
    virtual core::Size   RenderSize() const = 0;
    virtual IRenderTarget* RenderTarget() = 0;                          // 覆盖层绘制，可为 nullptr
};

class IRenderTarget {                     // 只提供最小绘制原语
public:
    virtual ~IRenderTarget() = default;
    virtual core::Size Size() const = 0;
    virtual void FillRects(const core::Rect* rects, std::size_t count, core::Color color) = 0;
    virtual void DrawLines(const core::Line* lines, std::size_t count, core::Color color) = 0;
};

class IEventSource {                      // 窗口/输入事件（后端翻译成统一事件）
public:
    virtual ~IEventSource() = default;
    virtual bool Poll(core::InputEvent& out) = 0;   // 无事件返回 false
    virtual bool QuitRequested() const = 0;
};

class IBackend {                          // 一个后端的全部能力
public:
    virtual ~IBackend() = default;
    virtual const char* Name() const = 0;
    virtual core::Status Initialize(const core::KeyValues& options) = 0;
    virtual void         Shutdown() = 0;
    virtual std::unique_ptr<IAudioSink>   CreateAudioSink() = 0;
    virtual std::unique_ptr<IVideoSink>   CreateVideoSink() = 0;
    virtual std::unique_ptr<IEventSource> CreateEventSource() = 0;
};
} // namespace av::output
```

**为什么 `IRenderTarget` 只有矩形和线？** 因为文字可以拆成矩形（见 5.4 位图字体）。这样任何后端（SDL 的 `SDL_RenderFillRect`、Qt 的 `QPainter::fillRect`、内存录制器）都能实现同一个接口，UI 逻辑一次编写到处运行，而且能在单元测试里用 `RecordingRenderTarget` 断言"进度条画在哪个像素区间"。

**事件为什么不用 SDL_Event？** 后端把 `SDL_Event` 翻译成 `core::InputEvent`（含 `core::KeyCode`），于是输入映射、点击暂停、进度条拖拽全是**纯逻辑**，可以脱离窗口做单元测试。这就是"接缝（seam）"的价值。

### 5.3 player：编排与门面

```cpp
namespace av {

enum class PlayerState { Idle, Opening, Ready, Playing, Paused, Seeking, Ended, Error };

struct PlayerObserver {                    // 未来在 Qt 里被转成信号
    virtual ~PlayerObserver() = default;
    virtual void OnStateChanged(PlayerState state) {}
    virtual void OnMediaOpened(const MediaDescription& media) {}
    virtual void OnPositionChanged(double seconds) {}
    virtual void OnDurationChanged(double seconds) {}
    virtual void OnError(const core::Status& status) {}
    virtual void OnEnded() {}
    virtual void OnFramePresented(double ptsSeconds) {}   // 便于诊断 A/V 偏差
    virtual void OnInputEvent(const core::InputEvent& event) {}
};

class Player {
public:
    explicit Player(std::shared_ptr<output::IBackend> backend,
                    const core::PlayerConfig& config = {});
    ~Player();

    core::Status Open(const std::string& url);   // 只打开与探测，不开始播放
    void         Close();

    core::Status Play();          // 开始/恢复；可重复调用
    void Pause();
    void TogglePlayPause();
    void SetSpeed(double speed);  // 0.25 ~ 4.0，内部做限幅
    void SeekTo(double seconds);  // 异步：下一帧生效，通过 OnPositionChanged 反馈
    void RequestQuit();

    PlayerState State() const;
    double      Position() const;
    double      Duration() const;
    double      Speed() const;

    void SetObserver(PlayerObserver* observer);
    void SetOverlayPainter(std::function<void(output::IRenderTarget&)> painter);
    void HandleInputEvent(const core::InputEvent& event);
    void Tick();                  // ★ 关键：由外部循环驱动（控制台 while / Qt QTimer）
};
} // namespace av
```

**`Tick()` 是整个设计里最值得审查的一处**：

- 旧 `AVPlay_1` 把主循环藏在播放器内部，容易出现"渲染在内部线程、事件泵又在另一处"的冲突；Windows 窗口响应问题和 Qt 主线程约束都来自这里。
- 这里反过来：**播放器不自带循环**，主循环由 App 提供。控制台里 `while (running) { player.Tick(); sleep(2ms); }`；Qt 里 `QTimer`（1~5ms）或 `QWindow::aboutToRender` 调 `Tick()`。同一套核心，两种宿主。
- `Tick()` 内部做四件事（都很短，不阻塞）：① 泵输入事件 → 产出 `Action`；② 若视频帧到期则呈现；③ 绘制覆盖层；④ 应用命令、上报位置变化。所有"等待"都在后台线程完成。

### 5.4 ui：覆盖层（进度条 / 点击暂停 / 倍速）

```cpp
namespace av::ui {

struct OverlayState {                 // 渲染所需的全部数据（由 player + 输入更新）
    bool   visible = true;
    bool   playing = false;
    double position = 0.0, duration = 0.0;
    double speed = 1.0;
    bool   dragging = false;
    core::Rect bar;                   // 布局结果
    core::Point cursor;
};

enum class ActionType { None, TogglePause, SeekTo, SeekRelative, SetSpeed, Quit };
struct Action { ActionType type = ActionType::None; double value = 0.0; };

struct InputMapConfig {               // 可配置：以后 Qt 里换键盘方案不用改逻辑
    core::KeyCode togglePause = core::KeyCode::Space;
    core::KeyCode seekForward = core::KeyCode::Right;   // +5s
    core::KeyCode seekBackward = core::KeyCode::Left;   // -5s
    core::KeyCode speedDown = core::KeyCode::LeftBracket;
    core::KeyCode speedUp   = core::KeyCode::RightBracket;
    double        seekStepSeconds = 5.0;
};

class OverlayController {             // 纯逻辑：输入 → 动作；命中测试；布局
public:
    explicit OverlayController(InputMapConfig map = {});
    Action Handle(const core::InputEvent&, OverlayState& state);  // 同时更新 state（悬停/拖拽）
    void   Layout(OverlayState& state, core::Size renderSize);
    void   Update(OverlayState& state, double dtSeconds);
    bool   NeedsRedraw() const;
};

class OverlayRenderer {               // 纯绘制：把 state 画到 IRenderTarget
public:
    void Draw(const OverlayState& state, output::IRenderTarget& target);
};

class BitmapFont {                    // 5x7 位图字体 → 输出矩形列表（后端无关）
public:
    static void Rects(char ch, core::Point origin, int scale, std::vector<core::Rect>& out);
    static void TextRects(std::string_view text, core::Point origin, int scale, std::vector<core::Rect>& out);
    static int  TextWidth(std::string_view text, int scale);
};
} // namespace av::ui
```

交互约定（可配置）：

| 输入 | 行为 |
|---|---|
| 单击画面 | 暂停/恢复（`clickToPause`） |
| 单击/拖拽进度条 | 跳转到对应时间（拖拽期间只更新 UI，松手才 seek，避免狂发 seek） |
| 滚轮上下 / `[` `]` | 倍速 -/+（0.25 起，档位 0.25/0.5/1/1.5/2/4） |
| `←` `→` | ∓5 秒 |
| `Space` / `P` | 暂停/恢复 |
| `Esc` | 退出 |
| 鼠标静止 2.5s | 隐藏覆盖层与光标（Qt 里同样逻辑复用） |

---

## 6. 线程模型与所有权

### 6.1 线程清单（后台 3 条 + 主线程 1 条）

| 线程 | 职责 | 阻塞点 |
|---|---|---|
| **main** | `Tick()`：泵事件、呈现视频帧、画覆盖层、应用命令、上报位置 | `SDL_Delay`/定时器（毫秒级，不阻塞窗口消息） |
| demux | 读包 → 按流分发到有界包队列；执行 seek 请求 | `av_read_frame`（可被中断回调打断） |
| audio | 出队音频包 → 解码 → 重采样 → 写 `IAudioSink`（水位背压）→ 更新时钟 | 水位等待、`IAudioSink::Write` |
| video decode | 出队视频包 → 解码 → 带世代号入帧队列 | 帧队列满 |

**为什么音频"解码+输出"合成一条线程，而视频解码单独一条？**
因为音频的节奏由设备决定（水位背压天然限速），合成一条最自然；视频必须先解码再等时钟，二者节奏不同。这是"按数据流特征分线程"，而不是"按类分线程"。将来若要更细（音频解码/输出分离），只需插一条队列，模块边界已经在那里。

### 6.2 同步策略

- 音频是主时钟（设备排队字节换算媒体时间），视频按 `pts - clock` 决定"等待/立即/丢帧"；
- 时钟由 `player/MediaClock` 单点持有：`SetBase/Reset/SetSpeed/Pause/Now()`；
- 当前实现是具体的 `MediaClock`，不是已经存在的 `SyncPolicy` 接口；“外部时钟同步/多人同步播放”属于未来扩展点，需要先抽接口再实现，不能把它误读成已具备能力。

### 6.3 所有权与锁的约定（写进代码注释，也是审查清单）

1. **谁创建谁释放**，一律 RAII（`AVFormatContext`/`AVCodecContext`/`AVFrame`/`AVPacket`/`SDL_*`/`SwrContext` 都包在各自的类里）；
2. **跨线程传递只用两种方式**：有界队列（移动语义）或 `std::atomic` 标志；不用裸指针共享可变对象；
3. **持锁期间不调用 FFmpeg/SDL/回调**（避免锁顺序死锁与长临界区）；
4. 帧/包在队里排队时**带世代号**；seek 时 `++generation`，消费端丢弃旧世代数据；
5. 观察者回调**只在主线程**（`Tick()` 内）触发，因此 Qt 侧不需要额外的跨线程 marshal 设计（Qt 适配层仍会用 `Qt::QueuedConnection` 兜底）。

### 6.4 暂停预览的背压规则

暂停时音频设备会停止消耗 `QueuedBytes`，但视频预览仍可能需要解码到 seek 目标。此时若 demux 线程继续把音频包塞进小队列，会在几十个包后卡住，视频也拿不到后续包。

当前规则是：

1. 只有 `seekPreviewActive == true` 时才允许丢弃音频包；
2. 只丢弃 `pts + duration <= seekTarget` 的包；
3. 目标及之后的包保留，恢复播放时无需重新 seek；
4. 预览帧选中或恢复播放后立即关闭该窗口；
5. 视频侧仍逐帧校验 generation，避免旧批次混入。

这比“暂停时把所有队列调大”更可控：内存有界，恢复位置和时间语义仍然明确。

---

## 7. 数据流与时序

### 7.1 正常播放

```
demux ──packetQ(audio)──▶ audio 线程: decode → swr → IAudioSink::Write ──▶ MediaClock.AudioWritten()
   └───packetQ(video)──▶ video decode ──frameQ(video)──▶ main::Tick: pts 到期? → IVideoSink::Present
                                                          └─ 画覆盖层 → 上报 OnPositionChanged
```

### 7.2 暂停 / 恢复

```
Pause():  state=Paused → IAudioSink::Pause(true) → MediaClock.Pause()
          普通暂停：保持队列和时钟；设备不消耗，时钟冻结
预览seek: seekPreviewActive=true → 只丢弃目标之前的音频包，让 demux 继续送视频包
Tick():   暂停时不呈现新帧，但仍泵事件、仍画覆盖层；预览时抓目标附近的一帧重绘
Play():   清除 seekPreviewActive，恢复音频设备与时钟
```

### 7.3 倍速

```
SetSpeed(x): MediaClock.SetSpeed(x) → 音频线程下一帧起用 out_rate = device_rate * x 重采样
             水位阈值按"媒体时间"换算：bytes = watermark * byteRate / x
             视频侧零改动（diff 公式里时钟已经按 x 走）
音调：默认"磁带式"（重采样变调）；[可选] atempo 滤镜保持音调（同一接口的另一个实现）
```

### 7.4 跳转（seek）

```
SeekTo(t): 记录 seekTarget + seekPreviewActive + ++generation（立刻，防旧帧混入）
  demux 线程: 被中断 → av_seek_frame(BACKWARD)
              → packetQ.Reset() / frameQ.Reset()
              → IAudioSink::Flush() → MediaClock.Reset(t)
  pipeline:   各自在新世代 flush decoder；旧世代 frame/write 一律丢弃
  暂停预览:   只丢目标之前的音频包；消费端抓到目标附近帧后清 seekPreviewActive
```

---

## 8. 错误处理、日志、配置

| 关注点 | 方案 | 理由 |
|---|---|---|
| 错误传播 | `core::Status` / `core::Result<T>` 返回值；不用异常穿过层边界 | 播放器里有大量"可预期失败"（文件缺失、解码不支持），异常会把控制流藏起来 |
| 错误上报 | `PlayerObserver::OnError(Status)` | UI 层决定"弹窗/日志/降级"，核心不关心呈现方式 |
| 日志 | `core::ILogSink` + 级别 + 分类标签；控制台实现 / 回调实现（Qt 里转 `qDebug`） | 日志是"依赖倒置"的经典例子：核心只依赖 sink 接口 |
| 配置 | `core::PlayerConfig`（强类型字段）+ `KeyValues backendOptions`（后端特有项） | 后端专有配置不能污染核心配置结构，否则每加一个后端都要改核心 |
| 句柄与设备 | 一律 RAII；`Close()` 幂等 | 现在 `AVPlay` 那种"错误路径手工清理 6 处"是 bug 温床（已修过一次泄漏） |

---

## 9. 测试策略（headless 优先）

| 测试对象 | 手段 |
|---|---|
| core（队列/状态/时间/配置） | 纯单元测试 |
| media（解封装/解码/seek/重采样） | `tests/test_media.cpp` 用真实样例文件做 headless 断言 |
| output 接口契约 | `null` 后端（`NullAudioSink` 按真实墙钟"消耗"队列，`NullVideoSink` 记录帧） |
| 时钟与同步 | null 后端 + 可注入的 `IClock`（测试里用假时钟，不 sleep） |
| ui 覆盖层 | `RecordingRenderTarget` 断言几何 + 直接调用 `OverlayController::Handle` 断言动作 |
| 端到端 | `tests/test_player_e2e.cpp` / `test_player_input.cpp` headless 回归；SDL 窗口用 `av_console --run-seconds` 冒烟 |

> 架构视角：**能全自动测试的前提是"后端可空"**。这不是额外工作量，而是把"窗口/声卡"推到接口后面的必然结果——这也是判断分层是否成功的试金石。

---

## 10. 演进路线（架构不阻碍的未来）

| 未来需求 | 需要改的地方 | 不需要改的地方 |
|---|---|---|
| 换成 Qt 界面 | 新增 `src/app/qt/` + `output/qt`（`QAudioSink`/`QPainter`/`QWidget` 事件） | 全部核心 |
| 集成进 Qt 的 FFmpeg UI（QImage/QVideoSink） | 只加一个 `IVideoSink` 实现 | media/player/ui |
| Linux 支持 | 新增 `output/alsa`、`output/pipewire`、`output/wayland_window` | 全部核心 |
| 硬件解码 | `media` 内新增 `HwDecoder` 策略 + `IVideoSink` 增加 dmabuf 视图 | player/ui |
| 外部时钟同步 | 新增 `SyncPolicy` 接口与实现 | 其他 |
| 多播放器实例 | `Player` 已无全局状态（SDL 全局初始化移到后端内部引用计数） | 其他 |

---

## 11. 已知取舍（写清楚，避免日后困惑）

1. **不暴露 FFmpeg 类型** → 跨层拷贝/包装有少量开销（视图是零拷贝，只有格式转换结构有成本）；
2. **文字用位图字体** → 现在不引入 SDL_ttf，代价是字形只有 5x7 位图（Qt 里换成 `QPainter::drawText`）；
3. **音频"解码+输出"合一线程** → 少一条线程与队列，代价是灵活度略低（扩展点已留）；
4. **`Tick()` 由宿主驱动** → App 必须写循环（控制台 6 行，Qt 一个 QTimer）；换来的是与 Qt/事件循环天然兼容；
5. **同步仍以音频为主时钟** → 与现有实现一致的策略，复杂场景（无音频流）需要 `ExternalClockSyncPolicy`，已留接口。
6. **`Open()` 仍同步执行 demux/探测** → 文件通常很快，但慢网络会占住宿主线程；已有中断回调，后续应升级为异步 Opening 状态机。
7. **中文 MSVC + Ninja 的 `/showIncludes` 前缀不可靠** → `tools/build.bat` 用 header stamp 在头文件变化时先 clean，换取正确性而不是追求错误增量。
