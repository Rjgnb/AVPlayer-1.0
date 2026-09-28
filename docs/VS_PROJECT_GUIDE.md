# Visual Studio 多项目使用指南

你打开的解决方案是：

`F:\project\audio_vioce\avplayer\bbb\avplayer.slnx`

这里的 `bbb` 是 **CMake 的构建目录**，不是第二份源码。真正的源码在 `F:\project\audio_vioce\avplayer\src`、`tests` 和 `cmake`。`bbb` 里的 `.vcxproj`、`.slnx` 都由 CMake 生成；不要直接在 VS 里编辑生成的项目文件，应该改 `CMakeLists.txt` 后让 CMake 重新生成。

---

## 1. 一句话理解这组项目

这是一个“分层库 + 一个运行程序 + 一个测试程序”的结构：

```text
av_console  (exe)  ── 真正启动播放器的程序
av_tests    (exe)  ── 自动测试整个播放器

avplayer     (lib) ── 播放编排：Player + 后台线程
avui         (lib) ── 覆盖层：进度条、文字、点击/拖拽逻辑
avoutput_sdl (lib) ── SDL 后端：窗口、SDL 音频、SDL 事件
avoutput     (lib) ── 后端接口 + null 后端 + 注册表
avmedia      (lib) ── FFmpeg 适配：demux / decode / resample / convert
avcore       (lib) ── 中性类型、时钟、队列、日志
```

依赖方向是单向的：

```text
av_console ──► avplayer ──► avmedia ──► avcore
     │             │           │
     │             └──────────►avoutput ──► avcore
     │                              ▲
     └────────────► avui ───────────┘
                         │
                         └────────► avcore

avoutput_sdl ──► avoutput ──► avcore
```

这不是“文件分类”，而是**把架构规则变成编译器约束**。例如：

- `avcore` 不知道 FFmpeg，也不知道 SDL；
- `avmedia` 可以用 FFmpeg，但不能用 SDL；
- `avoutput` 只定义接口，不知道窗口和声卡；
- `avoutput_sdl` 才允许包含 `<SDL.h>`；
- `avplayer` 只通过 `avoutput` 接口驱动设备；
- `av_console` 决定“本次使用哪个后端”。

如果某个文件写错了依赖，比如在 `avcore` 里直接 `#include <SDL.h>`，链接依赖和项目编译顺序会立刻暴露它。

---

## 2. Solution Explorer 里每个项目是干什么的

| VS 项目 | 类型 | 你什么时候打开它 | 关键文件 |
|---|---|---|---|
| `avcore` | 静态库 | 看基础类型、时钟、队列、日志 | `BoundedQueue.h`、`MediaClock.*`、`Types.h` |
| `avmedia` | 静态库 | 看 FFmpeg、解封装、解码 | `Demuxer.*`、`Decoder.*`、`AudioResampler.*` |
| `avoutput` | 静态库 | 看后端接口、null 后端、注册表 | `Backend.h`、`AudioSink.h`、`VideoSink.h` |
| `avoutput_sdl` | 静态库 | 看真实窗口/音频/输入如何实现 | `SdlBackend.*`、`SdlVideoSink.*`、`SdlAudioSink.*` |
| `avplayer` | 静态库 | 看播放器状态机、线程编排、seek | `Player.*`、`SharedState.h`、三条 `*Pipeline.*` |
| `avui` | 静态库 | 看进度条、输入动作、覆盖层绘制 | `Overlay.h`、`OverlayController.*`、`OverlayRenderer.*` |
| `av_console` | 可执行程序 | 运行真实播放器 | `main.cpp`、`ConsoleHud.h` |
| `av_tests` | 可执行程序 | 自动测试；调试播放核心 | `tests/test_*.cpp` |
| `ALL_BUILD` | CMake 生成 | 通常不要直接使用 | 由 CMake 生成 |
| `ZERO_CHECK` | CMake 生成 | 通常不要直接使用 | 检查 CMake 是否要重新生成 |
| `RUN_TESTS` | CTest 生成 | 运行注册的 CTest | 测试入口包装 |
| `Continuous` / `Experimental` / `Nightly` / `NightlyMemoryCheck` | CTest 生成 | 通常可忽略 | CDash 测试仪表盘目标 |

`avcore`、`avmedia` 等是 **STATIC library**，不是 exe。单独按 F5 不能“运行”它们；需要启动 `av_console` 或 `av_tests`，链接器会把它们组合进可执行文件。

---

## 3. 第一次打开后怎么用

### 3.1 先确认启动项目

在 Solution Explorer 中右键：

- 想运行播放器：把 `av_console` 设为 Startup Project。
- 想跑测试/调试播放核心：把 `av_tests` 设为 Startup Project。
- 不要把 `ALL_BUILD` 设为 Startup Project；它是构建聚合目标，不是你的程序。

### 3.2 调试 `av_console`

在 `av_console` 的项目属性里设置调试参数，例如：

```text
F:\project\audio_vioce\1-zzitai-480P-AVC 00_00_00-00_00_05.mp4 --backend sdl
```

推荐断点顺序：

1. `src/app/console/main.cpp` 的 `main()`：看宿主如何注册后端、创建 Player；
2. `src/player/src/Player.cpp` 的 `Open()` / `Play()`：看一次会话怎么建立；
3. `src/player/src/SharedState.h`：看主线程和三条后台线程共享了什么；
4. `src/player/src/PacketPump.cpp`：看包如何从容器进入音频/视频队列；
5. `src/player/src/AudioPipeline.cpp`、`VideoPipeline.cpp`：看解码、背压、时钟和 seek；
6. `src/player/src/Player.cpp` 的 `Tick()`、`PresentDueFrame()`：看主线程如何呈现；
7. `src/output/sdl/src/SdlVideoSink.cpp`、`SdlAudioSink.cpp`：看视频/音频设备如何真正提交；
8. `src/ui/src/OverlayController.cpp`：看鼠标/键盘如何变成 `Action`。

### 3.3 调试 `av_tests`

把 `av_tests` 设为 Startup Project，F5。它不需要窗口和声卡，适合学习并发和 seek：

- `test_player_e2e.cpp`：打开、播放、暂停、seek、倍速、Ended；
- `test_player_input.cpp`：点击暂停、拖进度条、预览目标帧、失焦取消拖拽；
- `test_media.cpp`：解封装、解码、重采样；
- `test_core_queue.cpp`：有界队列、关闭、中止、reset、丢弃前缀。

VS 的 Test Explorer 使用 CMake 生成的测试项目；如果想直接看断言输出，运行 `av_tests` 控制台程序更直观。

### 3.4 使用“文件夹”而不是“项目”

CMake 给 target 设置了 `FOLDER "avplayer/core"` 等属性，所以 Solution Explorer 可以按逻辑分组显示。分组只是视觉组织，真正决定编译顺序的是 `BuildDependency` 和 `target_link_libraries`。

你可以在 VS 里展开：

```text
avplayer/core
avplayer/media
avplayer/output
avplayer/player
avplayer/ui
avplayer/app
avplayer/tests
```

每个组对应一个源码目录和一个构建 target。

---

## 4. 为什么不像你以前那样“一整个项目”？

单项目把所有 `.cpp` 都塞进同一个编译目标，短期省事，但会带来四个问题：

1. **依赖没有边界**：任何文件都能随手 `#include <SDL.h>` 或 `<libavcodec/avcodec.h>`，几个月后就分不清谁依赖谁；
2. **替换驱动困难**：窗口、FFmpeg、UI、测试代码混在一起，换后端容易改到播放核心；
3. **编译粒度差**：改一个头文件可能让整个项目重编译，错误也难以定位；
4. **测试和运行程序捆绑**：无法自然地做“无窗口、无声卡”的 CI，也难让测试只链接需要的那几层。

多项目的收益是：

- **依赖方向可证明**：`avcore` 的链接列表里没有 FFmpeg/SDL；
- **变化轴隔离**：换 SDL 只动 `avoutput_sdl`，上层的 Player 不动；
- **并行/增量构建**：CMake/VS 知道哪些 target 需要重编；
- **可测试性**：`av_tests` 可以独立链接 null 后端和相关层；
- **为 Qt 做准备**：以后添加 `avoutput_qt`，不会和 `avoutput_sdl` 的窗口代码纠缠；
- **教学与维护**：项目的名字直接告诉你“这一层的责任是什么”。

这和你以前单项目写法的区别，不是“文件多了”，而是**把依赖、责任和变化点画进了工程文件**。

---

## 5. 你要改一个东西时，应该进哪个项目？

| 需求 | 首先改 | 通常不需要改 |
|---|---|---|
| 新的时间格式化 | `avcore` | media/output/player |
| 新的解封装/解码格式 | `avmedia` | UI/SDL |
| 新播放控制接口 | `avplayer/include/av/Player.h` + `Player.cpp` | 具体后端 |
| 新的窗口/音频驱动 | 新建 `avoutput_xxx` | Player/pipelines |
| 换 SDL 为 Qt | `avoutput_qt` + `av_console` 注册 | `avcore`、`avmedia`、`avplayer`、`avui` |
| 进度条样式/点击区域 | `avui` | FFmpeg/SDL 后端 |
| 鼠标键盘映射 | `avui::InputMapConfig` + `OverlayController` | `avmedia` |
| 端到端回归 | `av_tests` | 生成项目文件 |

规则：先问“这是哪条变化轴”，再打开对应项目。不要因为一个功能想放在 `Player` 里，就绕过已有的 target 边界。

---

## 6. VS 实操建议

- **只打开 `avplayer.slnx`**，不要在 `bbb` 里直接找源码编辑；真正的源码路径由 CMake 引用到上一级目录。
- **不要手动编辑 `.vcxproj`/`.slnx`**；修改 `CMakeLists.txt` 后重新 Configure/Build，CMake 会更新它们。
- **第一次建议先构建 `ALL_BUILD`**，确认所有项目能编过；之后调试时只关注 `av_console` 或 `av_tests`。
- **调试多线程时打开 Threads 窗口**，主要看 demux、audio decode/output、video decode、main 四条线程。
- **断点优先打在接口边界**：`Player::Tick()`、`PacketPump::Run()`、`AudioPipeline::Run()`、`VideoPipeline::Run()`，而不是一开始就钻进 FFmpeg 内部。
- **不要把 `bbb` 加入源码版本控制**；它只是构建产物目录。仓库的 `.gitignore` 已忽略这一类别目录。

如果你希望减少 VS 里看到的生成项目，可以在 CMake 中关闭暂时不需要的测试/控制台选项，但建议至少保留 `avcore`、`avmedia`、`avoutput`、`avplayer`、`avui` 和 `av_console` 来理解完整链路。

---

## 7. 推荐的阅读路线

```text
av_console/main.cpp
        ↓
av::Player (Player.h)
        ↓
SharedState.h
        ↓
PacketPump.cpp ──► AudioPipeline.cpp / VideoPipeline.cpp
        ↓                         ↓
   avoutput 接口            avmedia 解码
        ↓
SdlAudioSink / SdlVideoSink

同时支线：
OverlayController.cpp ──► Action ──► Player
test_player_*.cpp ──────► 在 VS 中断点验证
```

先理解“数据怎么流、谁拥有什么、在哪个线程做什么”，再去读 FFmpeg/SDL 的 API 细节。这样看到很多类时，心里会有一张固定的地图。
