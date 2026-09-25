# 平台与框架对照：SDL / Qt / Linux 各用什么

> 回答三个问题：
> 1) Qt 里的音视频驱动和 SDL 有什么不同？
> 2) Qt 里还需要 SDL 吗？
> 3) Linux 上又会用到什么？
>
> 结论先给：**Qt 不需要 SDL**；我们的代码把这两者都放在"后端"里，换掉不影响播放核心。

## 1. 先看清 SDL 到底是什么

SDL2 不是"音视频框架"，它是**一层很薄的跨平台系统封装**：

```
                 ┌──────────────────── SDL2 ────────────────────┐
应用 ── SDL API ─┤ 窗口/输入: Win32 · X11 · Wayland · Cocoa      │
                 │ 音频:      WASAPI · ALSA · PulseAudio · CoreAudio
                 │ 渲染:      D3D11 · OpenGL · Metal · 软件        │
                 └───────────────────────────────────────────────┘
```

所以"用 SDL"并不等于"用某套音视频驱动"：SDL 内部照样要落到 WASAPI/ALSA/X11 上。
它省掉的是**平台适配代码**，代价是**你必须接受它的消息泵和回调模型**。

## 2. Qt 里有什么（对照表）

| 能力 | SDL2（本项目的 `output/sdl`） | Qt6（未来的 `output/qt`） |
|------|------------------------------|--------------------------|
| 窗口 | `SDL_CreateWindow` | `QWidget` / `QOpenGLWidget` / `QQuickItem` |
| 画一帧 | `SDL_UpdateTexture` + `SDL_RenderCopy` | `QImage` + `QPainter::drawImage`，或 `QOpenGLTexture::setData` |
| 提交显示 | `SDL_RenderPresent`（可能等 vsync） | `update()` 触发 `paintEvent`（Qt 自己合帧） |
| 音频输出 | `SDL_OpenAudioDevice`（回调/队列） | `QAudioSink` + `QIODevice`（**拉模式**：Qt 来读你的数据） |
| 键盘/鼠标 | `SDL_PollEvent` | `keyPressEvent` / `mouseMoveEvent`（事件对象） |
| 缩放/DPI | `SDL_WINDOWEVENT_RESIZED` | `resizeEvent` / `QScreen::devicePixelRatio` |
| 字体 | 需要 SDL_ttf 或自己画 | `QPainter::drawText`（自带字体栈） |
| 线程约束 | 窗口/消息必须在创建它的线程 | **GUI 对象只能在主线程**（和本项目约定一致 ✅） |

**关键差异有两点：**

1. **事件模型**：SDL 是"你主动 `PollEvent` 拉"，Qt 是"框架推给你"。
   我们的设计早就把这件事抽象掉了：`Player::Tick()` 内部调 `IEventSource::Poll()`，
   Qt 宿主里同一个 `Tick()` 由 `QTimer` 驱动；Qt 的事件到得更"推"一些，
   直接调 `Player::HandleInputEvent(event)` 即可（公开接口，就是给宿主用的）。
2. **音频模式**：SDL 常用"回调/队列推模式"，`QAudioSink` 是"拉模式"
   （`QAudioSink` 调用你 `QIODevice::readData`）。我们的 `IAudioSink::Write(PCM)` 是推模式，
   在 Qt 里实现成"写进一个内部 `QIODevice` 的缓冲区"就行 —— 接口不用改，
   因为 `QueuedBytes()` 本来就要求后端自己报"还有多少没播出去"。

## 3. Qt 里还需要 SDL 吗？

**不需要，而且不建议混用。** 原因：

- 功能重叠：窗口、输入、音频 Qt 全都有；
- 两套消息循环会互相打架（SDL 也要处理 Windows 消息），
  典型症状就是你在旧代码里遇到过的"播放时窗口不响应"；
- 依赖更重：多带一个 `SDL2.dll` 和一套线程模型。

真正该复用的是**我们自己的分层**：`Player` / `core` / `media` / `ui` 一行不用改，
只写一个 `output/qt` 后端（约 500 行，见 `QT_INTEGRATION.md`）。

> 反过来说：如果你想要"不依赖 Qt 也能跑"的版本，SDL 后端就是那个版本 —— 这正是
> "后端可替换"的价值：**同一个播放内核，两种宿主。**

## 4. Linux 上会用到什么

按"从内核到应用"的顺序：

| 层 | 选择 | 说明 |
|----|------|------|
| 显示 | **Wayland**（新）/ **X11**（旧，兼容性最好） | Qt 里自动处理；SDL 里 `SDL_VIDEODRIVER=wayland/x11` |
| 无桌面环境 | **KMS/DRM** + GBM/EGL | 嵌入式/机顶盒；SDL 的 `kmsdrm` 后端 |
| 音频（内核） | **ALSA** (`/dev/snd/*`) | 唯一的 Linux 音频内核接口 |
| 音频（用户态） | **PulseAudio** 或 **PipeWire** | 桌面默认。PipeWire 已在新发行版取代 PulseAudio；它兼容 PulseAudio API |
| 硬件解码 | **VA-API**（Intel/AMD，`/dev/dri/renderD128`）、**V4L2 M2M**（树莓派等 ARM） | FFmpeg 里对应 `AV_HWDEVICE_TYPE_VAAPI` / `drm` |
| 播放上层 | Qt Multimedia（内部用 GStreamer 或 FFmpeg）、**GStreamer**（自己就是框架）、**MPV**、直接 **FFmpeg** | 学原理建议直接 FFmpeg（就是本项目） |

对应到我们的接口：Linux 宿主和 Windows 宿主**只在 `output/*` 这一层不同**，
`media` 层的硬件解码开关（VA-API）也在 media 层内部，接口不变。

> 小知识：Windows 上是 WASAPI（音频）+ D3D11VA/DXVA2（硬解）+ Win32 消息循环；
> macOS 上是 CoreAudio + VideoToolbox + Cocoa。**这三套平台差异全部被"后端"吸收**，
> 所以 `Player` 里不会出现 `#ifdef _WIN32`。

## 5. 一句话对照总结

| 问题 | 答案 |
|------|------|
| Qt 里音视频要自己写驱动吗？ | 不用驱动，但**要自己写"输出后端"**：音频用 `QAudioSink`，画面用 `QWidget`/`QPainter` |
| Qt 会帮你做 A/V 同步吗？ | **不会**。`QAudioSink` 只负责出声，媒体时钟（本项目 `MediaClock`）仍然要自己算 |
| Qt 里还要 SDL 吗？ | 不要。用 `output/qt` 替换 `output/sdl`，`Player` 零改动 |
| 换到 Linux 要改什么？ | 只改/新增 `output/linux`（ALSA/PipeWire + X11/Wayland）；`media` 换 VA-API 开关 |
| 后端接口会不会限制性能？ | 不会：接口只传 `VideoFrameView`（指针+stride），没有拷贝；渲染怎么加速由后端自己决定 |