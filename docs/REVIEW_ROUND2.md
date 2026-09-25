# 第二轮审查：实现、并发与异常路径复核

> 审查日期：2026-09-26  
> 审查对象：第一轮冻结后的真实实现、线程交互、seek/暂停/EOF/设备失败路径、构建可重复性。  
> 当前结论：**通过（核心回归为 40/40）。** 已修复的 P0/P1 问题见下文；仍然保留的限制已明确列为后续项。

---

## 1. 复核方法

本轮不只看代码，还按以下顺序复现和验证：

1. 干净构建，避免旧对象文件污染结论；
2. 运行全部 headless 测试；
3. 用测试过滤运行播放器路径；
4. 对失败用例加最小阶段日志，只保留能定位证据；
5. 修复后删除诊断日志，再做干净构建；
6. 重复运行并发相关用例，确认不是偶发通过；
7. 对每条修复补一个更接近根因的测试或注释。

验证结果：

- `avplayer\tools\build.bat`：通过；
- `avplayer\build\bin\av_tests.exe`：`40 passed, 0 failed`；
- 全量 headless 回归连续重复 10 轮：每轮均为 `40 passed, 0 failed`；
- `Player_` 过滤测试：多次运行通过；
- 暂停拖进度条预览：实际选到目标帧（约 `pts=3.067s`，目标约 `3.85s` 的预览用例约束）；
- 构建脚本头文件变更自动清理：已验证。
- SDL 真实窗口冒烟：`--backend sdl --no-audio --run-seconds 2` 退出码 0，呈现 73 帧、丢帧 0，窗口事件泵正常退出。
- SDL 真实音频冒烟：`--backend sdl --no-video --run-seconds 2` 退出码 0，F32/48kHz/2ch 设备正常打开并播放。

---

## 2. 已修复问题

### R2-01 [P0] 音频 planar 格式被按 packed 读取

**症状：** 播放器起播后立即 `0xC0000005`。

**根因：** 容器的 `codecpar->format` 只被映射成 `core::SampleFormat::F32`，丢掉了 `FLTP`（planar）和 `FLT`（packed）的区别。`AudioResampler` 因此按 packed FLT 配置 `swr_convert`，却把 AAC 解码帧的 planar `extended_data` 交给它读取，FFmpeg 越界访问平面指针。

**修复：**

- `AudioResampler` 内部保存实际 `AVSampleFormat`；
- 首帧到来时以 `AVFrame::format` 为准，必要时重建 `SwrContext`；
- 新增 `Media_音频重采样_按真实平面布局读取FLTP`，用左右声道不同常量验证 planar 布局没有被当成 packed。

**文件：** `src/media/src/AudioResampler.cpp`、`tests/test_media.cpp`

---

### R2-02 [P0] 暂停 seek 预览被音频队列背压卡死

**症状：** 暂停后拖进度条，视频解码只读到约 1.7s 就停住；目标 3.85s 的预览帧永远到不了，20 秒超时。

**根因：** 暂停时音频线程等待恢复，不再消费 `audioPackets`。demux 线程读到约 64 个音频包后被有界队列挡住，无法继续读取视频包。`PushWaiting` 的 10ms 超时只能响应 seek/退出，不能解决"seek 完成后仍没有音频消费者"的持续背压。

**修复：**

- `SharedState` 增加 `seekPreviewActive`，只在暂停预览期间打开；
- 音频侧和 demux 侧可调用 `DiscardAudioBefore(target)`；
- `BoundedQueue::DropFrontWhile` 只丢弃目标之前的包，目标及之后的音频保留；
- pipeline 逐帧检查新世代，旧批次不能再穿过 queue reset；
- `SeekTo` 同步清除主线程中的 `pendingFrame`，避免旧帧在新 seek 后趁机呈现。

**关键原则：** 暂停不是"所有消费者都停"；预览是一个短期、可解释、可结束的数据窗口。不能靠放宽所有队列或无限等待来掩盖它。

**文件：** `src/core/include/av/core/BoundedQueue.h`、`src/player/src/SharedState.*`、`src/player/src/AudioPipeline.cpp`、`src/player/src/PacketPump.cpp`、`src/player/src/Player.cpp`

---

### R2-03 [P1] B 帧批次中的旧世代帧污染新 seek

**症状：** seek 后预览消费者持续收到旧世代帧，队列看似已 reset，实际又被旧解码批次重新灌入。

**根因：** `VideoPipeline::PushFrames()` 只在队列满并重试时检查 generation。一个 `Decode()` 可能一次返回多帧；批次开始时世代正确，不代表批次内每一帧处理时仍正确。

**修复：**

- 每个 frame 入队前逐帧检查 generation；
- `reset` 后旧批次直接返回，不再进入新队列；
- 保留 `pendingFrame` 的主线程清理，覆盖"已取队列但未呈现"的窗口。

**文件：** `src/player/src/VideoPipeline.cpp`、`src/player/src/Player.cpp`

---

### R2-04 [P1] `avformat_flush()` 被误当作 seek 的一部分

**症状/风险：** 跳转后可能只读到目标前的一小段，随后提前 EOF；这不是队列容量问题，而是 demuxer 解析状态被额外清掉。

**根因：** `avformat_flush()` 的语义是丢弃字节流解析器内部缓冲，用于不连续流重新同步，不是 seek API。`av_seek_frame()` 已经负责读位置和内部包队列。

**修复：**

- 删除 `Demuxer::SeekTo()` 中额外的 `avformat_flush()`；
- 解码器缓冲仍由各 pipeline 在新世代中调用 `Decoder::Flush()`；
- 保留清晰注释，避免以后又把"seek 后乱掉"归因到 demux 缓冲。

**文件：** `src/media/src/Demuxer.cpp`

---

### R2-05 [P2] NV12 只填了一个平面

**症状/风险：** 后端支持 NV12 时，SDL 的 `SDL_UpdateNVTexture` 需要两个平面，但视图只填 `planes[0]`，会造成空指针或错误图像。

**修复：**

- `MakeVideoView()`：NV12 填 2 个平面；
- `VideoConverter::Convert()`：NV12 输出填 2 个平面；
- YUV420P 仍为 3 个平面，BGRA/RGBA 为 1 个。

**文件：** `src/media/src/Frame.cpp`、`src/media/src/VideoConverter.cpp`

---

### R2-06 [P1] 线程创建异常没有回收

**症状/风险：** `StartSession()` 在创建 demux 线程后再创建音频/视频线程失败时，异常会逃出 `Play()`，而已经启动的线程仍持有 `Player::Impl` 指针。

**修复：**

- 线程创建段包在 `try/catch (const std::exception&)` 中；
- 失败时调用 `StopSession()` 回收队列和已启动线程；
- 将异常翻译为 `core::Status{Internal}`；
- 清除启动失败造成的 `quitRequested`，避免宿主误以为用户要求退出。

**文件：** `src/player/src/Player.cpp`

---

### R2-07 [P1] 中文 MSVC 导致增量构建对象布局不一致

**症状：** 修改 `SharedState.h` 后，世代号读出 `0xCCCC...`；清理后立即恢复正常。这不是运行期数据损坏，而是 CMake/Ninja 的 `/showIncludes` 本地化前缀编码不匹配，旧对象文件没有被重新编译。

**修复：**

- 新增 `tools/check_header_changes.ps1`；
- `tools/build.bat` 在头文件比上次成功构建 stamp 更新时先 `clean`；
- 构建成功后更新 stamp；
- `build.bat` 保持 ASCII-only，避免 cmd.exe 在不同代码页下解析中文脚本出错。

**取舍：** 头文件改动会触发一次全量重建；源码文件改动仍由 Ninja 正常增量构建。对一个以"正确性优先"的教学项目，这比静默链接错误对象更合适。

**文件：** `tools/build.bat`、`tools/check_header_changes.ps1`

---

## 3. 并发复核清单

### 3.1 状态所有权

| 状态 | 唯一写者/持有者 | 其他线程如何看 |
|---|---|---|
| `Player::Impl::state_` | 主线程（生命周期） | 线程持有 `SharedStatePtr`，在 `StopSession()` join 前不会释放 |
| `SharedState::paused` | 主线程写 | atomic 读 |
| `SharedState::seekRequest` | 主线程写，demux 线程 exchange 取走 | atomic |
| `seekTarget` | 主线程写，pipeline 读 | atomic |
| `generation` | 主线程递增，各 pipeline 读 | atomic |
| packet/frame 队列 | 生产者/消费者 | `BoundedQueue` 内部 mutex |
| `audioSink` / `videoSink` | 主线程创建和关闭 | 线程启动前建立、join 后关闭，运行期不替换 |

### 3.2 退出顺序

`StopSession()` 的顺序是：

1. 置 `aborting` / `quitRequested`；
2. `Abort()` 三条队列；
3. `WakeAll()`；
4. join 所有后台线程；
5. 关闭 sink；
6. 清 pending/last frame。

这个顺序保证后台线程不会在 sink 已经关闭后继续写设备。`Close()` 可重复调用，重复时队列 Abort、join 空集合、sink Close 都保持幂等。

### 3.3 剩余竞态说明

- 当前 `Player` 明确要求所有 public 方法由主线程调用；如果宿主从别的线程调 `SeekTo()`，不提供额外保证。
- `BackendRegistry` 运行期注册不保证多线程安全；应在创建 Player/线程之前完成注册。
- `Open()` 仍然同步；中断回调已经能打断 FFmpeg 的打开/探测，但 UI 若要在打开期间保持动画，后续应做异步 Open 状态机。

---

## 4. 异常与提前 EOF 复核

| 路径 | 当前行为 | 审查结论 |
|---|---|---|
| 文件不存在 / demux 打开失败 | `Open` 返回错误，state 到 `Error`，不发线程 | 通过 |
| 音频设备打开失败 | 关闭该 sink，降级纯视频，错误进入 observer | 通过 |
| 视频输出打开失败 | 关闭该 sink，降级纯音频；无 audio 时墙钟 | 通过 |
| 单个音频/视频包解码失败 | 记录错误后继续消费，不把整场播放打断 | 通过 |
| demux EOF | 关闭包队列，消费者 drain，最后进入 Ended | 通过 |
| seek 时 demuxer 返回 `AVERROR_EXIT` | 视为被新 seek/退出打断，不升级为致命错误 | 通过 |
| 暂停中 seek | 打开短期音频丢弃窗口，只丢目标之前的包 | 通过 |
| `std::thread` 创建失败 | 回收已启动线程并返回 `Status` | 通过 |
| observer/overlay 回调抛异常 | 当前会穿过 `Tick()` | **保留风险**：宿主必须遵守不抛异常约定 |

---

## 5. 仍未关闭的后续项

按优先级排序，不影响本轮通过：

1. **异步 Open（P1）**：把 `Open` 拆成 `Opening` 后台任务 + `Ready/Error` 回调，避免慢网络阻塞 GUI。
2. **动态格式变化（P2）**：视频/音频中途变分辨率、像素格式或通道布局时，后端应重协商；当前主要覆盖起播和音频首帧。
3. **回调异常隔离（P2）**：在 observer/overlay 边界捕获异常并转成 `OnError`，避免宿主 bug 杀掉播放器。
4. **多实例后端注册（P3）**：注册表若要运行期并发使用，需要加锁或改为 immutable registry。
5. **更高倍速的队列容量策略（P3）**：4x 倍速下按媒体秒计算的水位与包队列容量需要再次压测。

---

## 6. 最终结论

第一轮冻结的接口和分层没有被迫推翻；第二轮发现的问题主要落在三个真实边界上：

1. **媒体格式的底层细节不能在抽象边界被抹掉**（planar vs packed）；
2. **暂停、seek、背压不能各自为政**（音频队列会反过来卡住 demux）；
3. **构建系统也会破坏并发正确性**（本地化 showIncludes 导致对象布局不一致）。

因此本轮结论是：**实现层通过复核；后续扩展应继续遵守第一轮的边界，不应用"再加一个全局锁"或"把逻辑塞回宿主"来解决新问题。**
