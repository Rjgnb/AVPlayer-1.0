# 后端（驱动）如何替换与配置

> 一句话：**换后端 = 写一个 `IBackend` 实现 + 一行注册，上层零改动。**
> 这不是口号，是编译期保证：`Player.h` 里没有 SDL / FFmpeg / Qt 的任何类型。

## 1. 三个接口就是一个后端

| 接口 | 职责 | 线程约定 |
|------|------|----------|
| `output::IAudioSink` | 音频输出：`NegotiateFormat` / `Open` / `Write` / `Pause` / `Flush` / `QueuedBytes` | 只在音频线程 |
| `output::IVideoSink` | 画面输出：`Open` / `Draw` / **`Present`** / `RenderTarget` | **只在主线程** |
| `output::IEventSource` | 事件输入：`Poll` / `QuitRequested` | 只在主线程 |

`IVideoSink` 刻意拆成 `Draw` + `Present` 两步：中间那一步就是**覆盖层绘制的插入点**
（`SetOverlayPainter` 的回调在这里被调用）。这样"进度条/时间显示"不需要后端知道任何 UI 逻辑。

## 2. 注册与选择

```cpp
// app 里：决定"这次用哪个后端"（app 是唯一知道具体实现的地方）
av::output::sdl::RegisterBackend();                  // 注册 "sdl"（第一个注册的 = 默认）
av::output::RegisterNullBackend();                   // 注册 "null"（测试/无人值守用）

// 选择：名字为空 = 默认后端
auto backend = av::output::BackendRegistry::Instance().Create("sdl");
```

命令行/配置覆盖：

```
av_console <媒体> --backend sdl           # 命令行
PlayerConfig::backendName                 # 配置结构体
PlayerConfig::backendOptions              # KeyValues：后端专有项，不污染通用配置
```

`backendOptions` 是关键设计：想加"选择音频设备"这种 SDL 专有选项时，
**不需要动 `PlayerConfig`**（否则每加一个后端就得改公共结构体，公共结构体会变成杂物间）：

```cpp
config.backendOptions.Set("audio_device", "扬声器 (Realtek)");
// 后端内部：if (options.Contains("audio_device")) { ... }
```

## 3. 加一个新后端的完整步骤

以 `output/qt` 为例：

1. 新建目录 `src/output/qt/`（`include/av/output/qt/QtBackend.h` + `src/`）。
2. `include/av/output/qt/QtBackend.h` 只暴露 `class QtBackend final : public IBackend` 和
   `void RegisterBackend(bool makeDefault = false);` —— **不要暴露 Qt 类型**（否则上层会被拖进 Qt 依赖）。
3. 在 `src/output/qt/CMakeLists.txt` 里 `target_link_libraries(avoutput_qt PUBLIC av::output PRIVATE Qt6::Multimedia Qt6::Widgets)`。
   `av::output` 只暴露"接口"，所以 Qt 头文件（`QAudioSink` 等）留在 `.cpp` 里即可。
4. 顶层 `CMakeLists.txt` 加一个开关 `AVPLAYER_BUILD_QT_BACKEND`。
5. app 里调 `av::output::qt::RegisterBackend()`。

不需要改的：`Player` / `SharedState` / 三条 pipeline / `core` / `media` / `ui`。

## 4. 已经在用的两个后端

| 名字 | 文件 | 用途 |
|------|------|------|
| `sdl` | `src/output/sdl/` | 真实窗口 + 声卡。`AcquireSdl/ReleaseSdl` 做引用计数，多实例共享 SDL 子系统 |
| `null` | `src/output/NullBackend.*` | headless：`NullAudioSink` 用**墙钟**模拟声卡消耗字节，`NullVideoSink` 记录帧与 pts，`NullEventSource` 可脚本化投喂事件 |

`null` 后端不是"占位"，它是**可测试性的来源**：AI/CI 里能跑真视频、验证 seek/倍速/结束判定，
以及（本次）验证"按键与拖拽真的能控制播放器"。见 `tests/test_player_input.cpp`。

## 5. 后端作者的自检清单

- [ ] `Initialize` 幂等吗？`Shutdown` 能被调用两次吗？
- [ ] `NegotiateFormat` 是否如实返回"我能接受的格式"（不支持的采样格式要在**设备打开前**暴露出来）？
- [ ] `Draw` 是否允许"尺寸变化"（窗口被放大缩小）？
- [ ] `Present` 是否只做提交，不做阻塞等垂直同步？（否则主线程会被拖住 → 窗口不响应）
- [ ] `QueuedBytes` 是否返回"设备里还没播出去的字节"（主时钟靠它，返回 0 会让时钟跑飞）？
- [ ] 窗口/渲染相关调用是否全部发生在主线程？