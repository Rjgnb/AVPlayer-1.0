# 第一轮审查：架构、接口与边界冻结

> 审查日期：2026-09-26  
> 审查对象：`avplayer/` 的分层、公开接口、线程模型、后端替换能力和 Qt 接缝。  
> 结论：**有条件通过，允许进入实现阶段。** 本文的 C1~C7 是实现前必须接受的约束；实现完成后由 `REVIEW_ROUND2.md` 对账。

---

## 1. 审查目标

这轮不看具体实现细节，先回答四个问题：

1. 旧 `AVPlay_1` 的问题是否真的是"缺接口"，还是只是代码写乱了？
2. `core / media / output / player / ui / app` 的依赖方向是否足够简单、可证明？
3. SDL、Qt、Linux 后端替换时，哪些东西必须稳定，哪些东西允许变化？
4. 这套接口能否让宿主（控制台、Qt）只负责"驱动 Tick + 接输入 + 接信号"，而不重新实现播放器？

本轮不讨论画质优化、硬件解码、字幕等增强功能；它们不应改变已冻结的边界。

---

## 2. 审查结论

### 2.1 通过项

**P1：分层是正确的，而且能被构建系统强制。**

`core` 只放中性类型与基础原语；`media` 是唯一允许碰 FFmpeg 的适配层；`output` 只定义音频、视频、事件接口；`player` 负责编排；`ui` 只做交互状态和绘制命令；`app` 才知道"本次用哪个具体后端"。依赖方向和目标链接方向一致，不是靠注释约束。

**P2：驱动可替换性是成立的。**

后端替换边界不是"换一个 SDL 类名"，而是 `IBackend` 一次性提供三类能力：

| 接口 | 解决的变化轴 |
|---|---|
| `IAudioSink` | WASAPI / SDL / QAudioSink / ALSA / PipeWire |
| `IVideoSink` | SDL_Renderer / QWidget / QOpenGLWidget / 平台纹理 |
| `IEventSource` | SDL 事件 / Qt 事件 / 测试脚本事件 |

`Player.h`、`SharedState`、pipeline 中不出现 SDL/Qt 类型；Qt 侧不需要为了复用播放内核而重新写 seek、时钟或背压逻辑。

**P3：`Draw + Present` 分两步是正确的。**

覆盖层必须在视频帧上传后、提交前插入。把 `IVideoSink` 做成"只上传并提交一帧"会迫使后端理解 UI；拆成 `Draw` / `Present` 后，`ui` 通过 `IRenderTarget` 只依赖矩形和线段，Qt 可用 `QPainter` 重画，SDL 可用 `SDL_RenderFillRect`，测试可用录制目标。

**P4：`Tick()` 由宿主驱动是正确的 Qt 接缝。**

播放器不自带 GUI 主循环；控制台用 `while + sleep`，Qt 用 `QTimer`。输入从 `IEventSource` 翻译成中性 `InputEvent`，状态从 `PlayerObserver` 回调出去。Qt 适配层只需要：

- `QTimer -> Player::Tick()`
- Qt 事件 -> `core::InputEvent`
- `PlayerObserver -> Qt signal`

这比"Player 内部开一个渲染线程，再从线程回调 GUI"安全得多。

**P5：null 后端不是测试补丁，而是接口验收器。**

如果播放器不能在没有窗口、没有声卡的情况下跑真实文件，说明 UI/设备逻辑还没有真正从核心中隔离。`NullBackend` 让端到端测试、CI、seek/倍速/结束判定都不依赖宿主环境。

**P6：世代号比布尔标志更适合 seek。**

seek 的难点不是"把 seek 请求交给 demux"，而是旧包、旧帧、旧音频写入不能穿过 reset 后混进新位置。`generation` 是跨线程数据的版本号，消费端只接受当前世代，方向正确。

**P7：播放时钟只有一个权威。**

`MediaClock` 统一音频主时钟/墙钟兜底，倍速只改时钟换算和音频重采样。这个选择避免了"seek 改一个变量、倍速改另一个变量、视频线程再自己算一套"的常见分叉。

---

## 3. 有条件通过：实现前必须接受的约束

### C1：公有 API 只在主线程调用

`Player`、`IVideoSink`、`IEventSource`、observer 回调都属于主线程契约。后台线程只能通过队列、atomic、`SharedState` 通信。Qt 里不要求锁 GUI 对象；控制台也不会从渲染线程碰 window。

**验收：** 代码注释、Qt 骨架和所有测试都按这个线程契约调用。

### C2：`media` 是 FFmpeg 的唯一边界

不允许 `player/ui/output` 直接包含 `AVFrame`、`AVPacket`、`AVCodecContext`。即使是"临时方便"也不行，否则换 FFmpeg 版本或引入硬件帧时会污染所有层。

**验收：** 新增第三方格式/解码能力只改 `media` 和该能力的后端视图。

### C3：所有阻塞都必须有可打断路径

队列满、设备水位满、`av_read_frame`、打开网络流、退出、seek 都属于"随时可能同时发生"的控制路径。无限等待必须以中断回调、超时轮询或可唤醒谓词收口。

**验收：** 暂停、seek、退出、设备失败四种情况下，后台线程最终都能返回；不能只靠"通常很快完成"。

### C4：seek 后必须做三层清理

1. demux 队列和 frame 队列 reset；
2. decoder 在自己的线程里 flush；
3. 旧世代的 packet/frame 在 push/write/take 时再校验一次。

只做第 1 层不够，因为一个 `Decode()` 可能已经吐出一整批 B 帧；只做第 2 层也不够，因为旧批次可能已进入队列。

**验收：** `SeekTo()`、`VideoPipeline::PushFrames()`、`AudioPipeline::WriteFrames()` 的世代检查互相闭合。

### C5：音频和视频是不同节奏，必须允许独立背压

暂停预览时视频需要继续解码到目标帧，但音频消费者可能已经暂停。不能假设"暂停"等于"所有 pipeline 都停"；需要一个明确的、短期的 seek preview 丢弃策略，且不能丢掉目标之后的音频。

**验收：** `test_player_input.cpp` 的暂停拖进度条用例在没有窗口的后端下通过。

### C6：异常只用于不可承受的边界，层间传递用 `Status`

文件不存在、解码失败、设备打开失败是可预期错误，必须通过 `core::Status` 返回。`std::thread` 创建失败属于运行时资源异常，不能在构造一半时逃逸到宿主；必须在 `StartSession` 内翻译为 `Status` 并回收已启动线程。

### C7：工程文件不是附属品

构建脚本、测试、文档、依赖目录都是设计的一部分。特别是 Windows 中文 MSVC 的 `/showIncludes` 本地化前缀会静默破坏头文件依赖，必须有可重复的构建门禁。

---

## 4. 明确不接受的方案

以下方案不在本工程中采用：

1. **Player 内部再建一个 UI 线程/GUI 循环。** 会与 Qt、SDL、Win32 消息循环重叠。
2. **把 `AVFrame*` 直接交给后端。** 生命周期和跨线程释放会变成口头约定。
3. **每个后端复制一份 seek/时钟/队列逻辑。** 后端只负责设备与绘制，不负责播放语义。
4. **把 SDL/Qt 选项塞进 `PlayerConfig` 的通用字段。** 后端专有项放 `KeyValues backendOptions`。
5. **暂停时把整个 pipeline 无限停住。** 暂停预览和恢复需要明确的背压协议。
6. **把头文件变化后的正确增量构建寄托在系统 locale 上。** 需要构建脚本自行保证。

---

## 5. 尚未在本轮解决的风险

这些不是接口冻结失败，但要在第二轮持续跟踪：

- `Player::Open()` 仍可能同步阻塞主线程；网络文件和慢设备需要后续异步 Open。
- 动态分辨率、动态像素格式变化目前只在部分路径处理。
- observer/overlay 回调如果抛异常，会穿过 `Tick()`；宿主必须遵守不抛异常约定，或后续加隔离层。
- 后端注册表默认按"进程启动后只读"使用，不承诺运行期多线程注册。

---

## 6. 放行条件

满足以下条件后允许进入实现：

- [x] `IBackend` / `IAudioSink` / `IVideoSink` / `IEventSource` 边界冻结；
- [x] `Player` 公开接口与主线程契约冻结；
- [x] `Tick()` 宿主驱动模型冻结；
- [x] `generation` seek 语义冻结；
- [x] null 后端与 headless 测试策略冻结；
- [x] Qt 只通过 observer + 事件输入接入，不反依赖 core。

**最终结论：有条件通过。** 条件不是"以后有空再做"，而是第二轮必须逐条验证的实现约束。
