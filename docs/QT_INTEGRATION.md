# Qt 集成指南（把 avplayer 接到 Qt 里）

> 目标：**`Player` / `core` / `media` / `ui` 一行都不改**，只新增一个 `output/qt` 后端和一个 Qt 宿主窗口。
> 下面给的是可落地的骨架，不是伪代码。

## 0. 整体形状

```
        ┌──────────────────── Qt 宿主（你的 MainWindow）────────────────────┐
        │ QTimer(2ms) ──► player.Tick()        keyPressEvent ► HandleInputEvent │
        │ PlayerObserver ──► emit 信号（给进度条/标签用）                     │
        └───────────────┬─────────────────────────────────────────────────┘
                        │  （只依赖 av::Player 这个头文件）
        ┌───────────────▼───────────────┐
        │ av::Player（播放内核，不变）    │
        └───────────────┬───────────────┘
                        │ IBackend / IAudioSink / IVideoSink / IEventSource
        ┌───────────────▼───────────────┐
        │ output/qt（新写，~500 行）      │  QAudioSink + QWidget/QPainter
        └───────────────────────────────┘
```

## 1. 视频输出：Draw / Present 正好对应 Qt 的绘制流程

`IVideoSink` 的两步设计在 Qt 里非常自然：

- `Draw(view)`：把 YUV/RGB 帧转到 `QImage` 并**存起来**（不提交）；
- `Present()`：`widget->update()`；真正的绘制发生在 `paintEvent`。

```cpp
// src/output/qt/src/QtVideoSink.h（示意）
class QtVideoSink final : public av::output::IVideoSink
{
public:
    explicit QtVideoSink(QWidget* widget) : widget_(widget) {}

    bool SupportsFormat(core::PixelFormat format) const override
    {
        // Qt 没有"YUV420P 直接上屏"的公共 API，统一走 RGB32：
        return format == core::PixelFormat::Bgra || format == core::PixelFormat::Rgba;
    }
    core::PixelFormat PreferredFormat() const override { return core::PixelFormat::Bgra; }

    core::Status Open(const core::VideoFormat& format, const av::output::VideoSinkConfig&) override;
    void         Close() override { image_ = QImage(); }
    bool         IsOpen() const override { return opened_; }

    core::Status Draw(const core::VideoFrameView& frame) override
    {
        if (frame.format.format != core::PixelFormat::Bgra) return core::Status::Ok();   // 交给 Player 的 sws 转换
        const QImage::Format qformat = QImage::Format_ARGB32;   // 小端下 = BGRA 字节序
        image_ = QImage(frame.planes[0], frame.format.width, frame.format.height,
                        frame.strides[0], qformat).copy();      // copy()：帧的缓冲随时会被复用
        return core::Status::Ok();
    }

    void Present() override
    {
        if (widget_ != nullptr) widget_->update();   // 真正画在 paintEvent 里
    }

    av::output::IRenderTarget* RenderTarget() override { return &target_; }
    core::Size RenderSize() const override { return core::Size{ widget_->width(), widget_->height() }; }

    const QImage& Image() const { return image_; }
    void          Paint(QPainter& painter, const QRect& target) const
    {
        if (!image_.isNull()) painter.drawImage(target, image_);
        target_.Replay(painter);          // 覆盖层：把记录下来的矩形/线段画上去
    }

private:
    QWidget*                     widget_ = nullptr;
    QImage                       image_;
    av::output::QtRenderTarget   target_;   // 实现 IRenderTarget，内部只记录
    bool                         opened_ = false;
};
```

```cpp
// 宿主窗口里
void VideoWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (sink_ != nullptr) sink_->Paint(painter, AspectFitRect(rect(), sink_->Image().size()));
}
```

> **为什么要 `.copy()`**：`VideoFrameView` 指向解码器的帧缓冲，`Draw` 返回后那块内存随时可能被复用，
> 而 Qt 的 `paintEvent` 可能在之后才发生。这一步拷贝是"零拷贝接口 + 延迟绘制"之间的必要代价；
> 想省掉它就用 `QOpenGLWidget` + `glTexSubImage2D`（把纹理上传也放在 `Draw` 里，`Present` 只 `update()`）。

## 2. 音频输出：QAudioSink 是"拉模式"，包一层就对了

```cpp
class QtAudioSink final : public av::output::IAudioSink
{
public:
    const char* Name() const override { return "qt-audio"; }

    core::AudioFormat NegotiateFormat(const core::AudioFormat& source) const override
    {
        core::AudioFormat out = source;      // QAudioSink 支持 U8/S16/S32/F32 交错
        out.format = core::SampleFormat::S16;   // 选 S16 最省事（有些设备不支持 F32）
        return out;
    }

    core::Status Open(const core::AudioFormat& format, const av::output::AudioSinkConfig&);
    void         Close() override;

    core::Status Write(const core::AudioFrameView& pcm) override
    {
        std::lock_guard lock(mutex_);                       // Qt 会从自己的线程 readData
        device_.Append(pcm);                                // 追加到内部 QIODevice 的缓冲
        if (sink_ != nullptr && sink_->state() == QAudio::SuspendedState) sink_->resume();
        device_.Notify();                                   // 唤醒 QAudioSink 来读
        return core::Status::Ok();
    }

    // 主时钟靠它：必须返回"设备里还没播出去的字节"
    std::size_t QueuedBytes() const override
    {
        std::lock_guard lock(mutex_);
        const qint64 inDevice = sink_ != nullptr ? sink_->bufferSize() - sink_->bytesFree() : 0;
        return static_cast<std::size_t>(device_.PendingBytes() + inDevice);
    }

    void Pause(bool paused) override { paused ? sink_->suspend() : sink_->resume(); }
    void Flush() override { std::lock_guard lock(mutex_); device_.Clear(); }

private:
    mutable std::mutex mutex_;
    PullDevice   device_;        // QIODevice 子类：readData 从缓冲里取数据
    QAudioSink*  sink_ = nullptr;   // 由 QAudioSink(device_) 构造
};
```

要点：
- **`QueuedBytes()` 必须准**。主时钟 = `最后写入的 pts - 排队字节 / 字节率`，这里报 0 会让时钟瞬间跑飞。
- `QAudioSink` 不是线程安全的，而 `Write` 发生在音频线程、`readData` 发生在 Qt 的线程 ⇒ **加锁**。
- 想在暂停时立刻停声，用 `suspend()`；恢复用 `resume()`。

## 3. 事件：Qt 的事件已经是"推"过来的

```cpp
// VideoWidget::keyPressEvent
void VideoWidget::keyPressEvent(QKeyEvent* event)
{
    core::InputEvent input;
    input.type = core::InputEventType::KeyDown;
    input.key  = MapKey(event->key());          // Qt::Key_Space -> core::KeyCode::Space
    input.isRepeat = event->isAutoRepeat();
    player_->HandleInputEvent(input);           // 公开接口，专门给宿主用
    QWidget::keyPressEvent(event);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent* event)
{
    core::InputEvent input;
    input.type     = core::InputEventType::MouseButtonUp;
    input.button   = core::MouseButton::Left;
    input.position = core::Point{ event->pos().x(), event->pos().y() };   // 注意与 scale 一致
    player_->HandleInputEvent(input);
}
```

`resizeEvent` → `core::InputEventType::WindowResized`（`OverlayController::Layout` 靠它重算进度条位置）。
关窗 → `core::InputEventType::Quit`（或者直接用 `Player::RequestQuit()`）。

## 4. 宿主主循环：一个 QTimer 就够了

```cpp
MainWindow::MainWindow()
{
    av::output::qt::RegisterBackend();
    backend_ = av::output::BackendRegistry::Instance().Create("qt");

    core::PlayerConfig config;
    config.windowTitle = "avplayer (Qt)";
    player_ = std::make_unique<av::Player>(std::move(backend_), config);

    // 覆盖层：和命令行版用的是同一个 controller / renderer
    player_->SetObserver(this);                          // MainWindow : public av::PlayerObserver
    player_->SetOverlayPainter([this](av::output::IRenderTarget& target) {
        renderer_.Draw(overlay_, target);
    });

    tickTimer_.setInterval(2);                            // 2ms ≈ 500Hz，够跟手又不烧 CPU
    connect(&tickTimer_, &QTimer::timeout, this, [this] { player_->Tick(); });
    tickTimer_.start();
}

// ---- PlayerObserver：翻译成 Qt 信号（Qt 侧的最佳实践）----
void MainWindow::OnPositionChanged(double seconds)
{
    if (!overlay_.dragging) overlay_.position = seconds;
    emit positionChanged(seconds);         // 给 QSlider / QLabel 用
}
void MainWindow::OnStateChanged(av::PlayerState state)
{
    overlay_.playing = (state == av::PlayerState::Playing);
    emit stateChanged(static_cast<int>(state));
}
void MainWindow::OnMediaOpened(const av::MediaDescription& media)
{
    overlay_.duration = media.durationSeconds;
    emit durationChanged(media.durationSeconds);
}
void MainWindow::OnInputEvent(const core::InputEvent& event)
{
    Apply(controller_.Handle(event, overlay_));   // 和命令行版同一个映射逻辑
}
```

**为什么这套在 Qt 里是安全的**：`Player` 的所有公开方法都要求**在主线程调用**，
回调也只在 `Tick()` 内发出 —— 而 `QTimer` 的回调就在主线程。
于是"信号槽跨线程 marshal"这类麻烦根本不会出现。

## 5. 覆盖层：想用 QPainter 画真字体

`ui` 层的 `OverlayState` 是**纯数据**，`OverlayController` 是**纯逻辑**，
所以你有两种选择：

- **A（省事，推荐先用这个）**：继续用 `ui::OverlayRenderer` + 位图字体。
  它只依赖 `IRenderTarget`，在 Qt 里照样能画，视觉与命令行版完全一致。
- **B（更漂亮）**：自己写 `QtOverlayPainter`，直接读 `OverlayState` 并用
  `QPainter::drawText` / `drawRoundedRect` 画。**不需要改 ui 层一行代码** ——
  这正是"状态、逻辑、绘制三者分离"的回报。

```cpp
void QtOverlayPainter::Paint(const av::ui::OverlayState& s, QPainter& painter, core::Size window)
{
    if (!s.visible) return;
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 进度条
    const QRectF track(s.bar.x, s.bar.y, s.bar.width, s.bar.height);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 60));
    painter.drawRoundedRect(track, 3, 3);
    if (s.duration > 0.0)
    {
        painter.setBrush(QColor(90, 170, 255));
        QRectF played = track;
        played.setWidth(track.width() * std::clamp(s.position / s.duration, 0.0, 1.0));
        painter.drawRoundedRect(played, 3, 3);
    }

    // 文字：Qt 自带字体栈，比位图字体好看
    painter.setPen(Qt::white);
    painter.setFont(QFont("Consolas", 12));
    painter.drawText(QPointF(16, window.height - 56),
                     QString::fromStdString(av::core::FormatTimecode(s.position) + " / " +
                                            av::core::FormatTimecode(s.duration) + "  " +
                                            av::core::FormatSpeed(s.speed)));
}
```

## 6. 落地顺序（建议）

1. `output/qt` 的 `QtAudioSink`（先用 `QAudioSink` 出声，画面先不做）→ 验证音频 + 主时钟。
2. `QtVideoSink` + `QWidget::paintEvent`（此时 `SupportsFormat` 只认 BGRA，
   让 `Player` 内部的 `VideoConverter`（sws）去做 YUV→BGRA 转换 —— 慢一点，但一次就通）。
3. 事件映射（键盘/鼠标/resize/关窗）。
4. 覆盖层：先用 A 方案，再按需换成 B 方案。
5. 性能不够再上 `QOpenGLWidget` / 硬解（`media` 层加 `AVHWDeviceType`，接口不变）。

> 每一步都能独立跑起来并看到结果 —— 这就是"分层 + 后端可替换"带来的好处：
> **不需要一次写完才能验证。**