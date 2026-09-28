# avplayer

一个**分层、可插拔后端**的音视频播放器（C++20 / FFmpeg / SDL2），从零手写线程编排与时序同步，用于学习播放器内核的工作原理。

不是 `ffplay` 的封装：解封装、解码、重采样、时钟同步、帧队列、管线线程全部自己实现，FFmpeg 只负责它真正擅长的那一层。

---

## 特性

- **播放控制**：播放 / 暂停 / 倍速（0.25×–4×）/ seek / 逐帧位置查询
- **画面覆盖层**：进度条、时间码、状态提示，用自绘位图字体渲染（不依赖 SDL_ttf）
- **交互**：键盘与鼠标（暂停、前后跳转、拖动进度条、滚轮调速）；暂停时拖动可预览目标画面
- **可插拔后端**：`sdl`（真实设备）与 `null`（headless，跑测试用）通过注册表选择，播放核心零改动
- **磁带式变速**：倍速在重采样层实现（缩放输入采样率交给 swr），音高随之变化
- **错误不崩溃**：错误经返回值传播；单个包解码失败不终止播放

## 架构

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

依赖只能向下。**这条规则由构建系统强制**：每个层次是独立的 CMake target，`target_link_libraries` 的可见性决定了谁能 include 谁 —— 想让 `app` 直接调 FFmpeg，就必须显式加依赖，摩擦即约束。

| 模块 | target | 依赖 |
|---|---|---|
| `src/core` | `avcore` | 无第三方 |
| `src/media` | `avmedia` | + FFmpeg |
| `src/output` | `avoutput` | 接口 + null 后端 + 注册表 |
| `src/output/sdl` | `avoutput_sdl` | + SDL2 |
| `src/player` | `avplayer` | + avmedia + avoutput |
| `src/ui` | `avui` | + avcore + avoutput 接口 |
| `src/app/console` | `av_console` | 唯一决定"用哪个后端"的地方 |
| `tests` | `av_tests` | 全部 |

设计理由、线程模型与时序细节见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## 依赖

| 需要 | 说明 |
|---|---|
| CMake ≥ 3.21 | |
| C++20 编译器 | MSVC / GCC / Clang 均可（本项目主要在 MSVC 上验证） |
| **FFmpeg + SDL2 开发包** | **不在本仓库内**，需自行准备 |

第三方 SDK 的路径只在 `cmake/Dependencies.cmake` 里出现一次，通过 CMake 变量 `AVPLAYER_SDK_ROOT` 指定，默认值为仓库**上一级**目录下的 `ffmpeg-dev-sdk`：

```bash
cmake -S . -B build -DAVPLAYER_SDK_ROOT=/path/to/ffmpeg-dev-sdk
```

该目录需包含 `include/`、`lib/`、`bin/`（`bin/` 里的 DLL 会被自动拷到输出目录）。

> ⚠️ 也就是说，**直接 clone 本仓库无法构建** —— 必须另外提供 SDK。若 SDK 不在默认位置，务必传 `-DAVPLAYER_SDK_ROOT`，否则 CMake 会以明确的错误信息中止。

## 构建

```bash
cmake -S . -B build -DAVPLAYER_SDK_ROOT=/path/to/ffmpeg-dev-sdk
cmake --build build
```

Windows + MSVC 上还有作者自用的包装脚本 `tools/build.bat`（内含本机 VS/CMake 路径，**需要按自己的环境修改**）：

```bat
tools\build.bat            :: 增量构建
tools\build.bat clean      :: 强制全量重编
tools\build.bat av_tests   :: 只构建某个 target
```

它会在头文件变更时自动清理旧目标文件 —— 中文版 MSVC 的 `/showIncludes` 前缀编码问题会让 Ninja 静默丢失头文件依赖，进而残留过期的 `.obj`。

### 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `AVPLAYER_BUILD_SDL_BACKEND` | `ON` | 构建 SDL2 输出后端 |
| `AVPLAYER_BUILD_CONSOLE_APP` | `ON` | 构建控制台演示程序 |
| `AVPLAYER_BUILD_TESTS` | `ON` | 构建单元测试 |
| `AVPLAYER_WARNINGS_AS_ERRORS` | `OFF` | 警告视为错误 |

构建完成后可执行文件在 `build/bin/`。

## 运行

```bash
build/bin/av_console <媒体文件> [选项]
```

| 选项 | 说明 |
|---|---|
| `--backend <名字>` | 输出后端（默认注册表第一项：`sdl` / `null`） |
| `--speed <倍速>` | 0.25–4.0（默认 1.0） |
| `--start <秒>` | 起始位置 |
| `--run-seconds <秒>` | 播放指定时长后自动退出（自测/回归用） |
| `--no-video` / `--no-audio` | 只播一路 |
| `--log <级别>` | `trace` / `debug` / `info` / `warn` / `error` |
| `--list-backends` | 列出已注册后端后退出 |

**播放中的操作**：空格或单击画面 = 暂停；`←` `→` = ∓5s；`↑` `↓` = ∓60s；`[` `]` 或滚轮 = 调速；拖拽进度条 = 跳转；`Esc` = 退出。

用 `null` 后端可以在无显示器/无声卡的环境下验证整条管线：

```bash
build/bin/av_console sample.mp4 --backend null --run-seconds 5
```

## 测试

```bash
build/bin/av_tests.exe
```

自带极简 TestHarness，不依赖任何测试框架。当前 **40 个用例全部通过**，覆盖队列语义、注入式时钟、解封装/解码、null 后端、位图字体、覆盖层渲染、端到端播放与交互。

部分用例需要一个样例视频：默认从**仓库上一级**目录找（`AVPLAYER_TEST_MEDIA_DIR`，编译期注入）。**找不到时这些用例会自动跳过并打印提示**，不会失败。

```bash
cmake -S . -B build -DAVPLAYER_TEST_MEDIA_DIR=/path/to/media-dir
```

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | 分层规则、接口清单、线程模型与时序（**先读这篇**） |
| [`docs/BACKENDS.md`](docs/BACKENDS.md) | 后端接口约定与新增后端的步骤 |
| [`docs/PLAN.md`](docs/PLAN.md) | 实现计划与阶段划分 |
| [`docs/LEARNING_GUIDE.md`](docs/LEARNING_GUIDE.md) | 按主题索引的阅读路线 |
| [`docs/PLATFORMS.md`](docs/PLATFORMS.md) | SDL / Qt / Linux 各用什么，为什么 |
| [`docs/QT_INTEGRATION.md`](docs/QT_INTEGRATION.md) | 把播放核心接到 Qt 窗口的具体做法 |
| [`docs/VS_PROJECT_GUIDE.md`](docs/VS_PROJECT_GUIDE.md) | 用 Visual Studio 多项目阅读与调试 |
| [`docs/REVIEW_ROUND1.md`](docs/REVIEW_ROUND1.md) · [`REVIEW_ROUND2.md`](docs/REVIEW_ROUND2.md) | 两轮设计/实现审查记录 |

## 平台

目前在 **Windows + MSVC** 上验证。代码本身是跨平台的（`core`/`media`/`player`/`ui` 不含任何平台 API，平台相关代码全部隔离在 `src/output/<后端>/` 内），但 Linux/macOS 尚未实测。

见 [`docs/PLATFORMS.md`](docs/PLATFORMS.md)。

## 许可证

未指定。
