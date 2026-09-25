# avplayer 实施计划（分阶段 + 验收标准）

> 审查记录：[第一轮：架构/接口冻结](REVIEW_ROUND1.md) · [第二轮：实现/并发/异常复核](REVIEW_ROUND2.md)

> 唯一纪律：**每个阶段结束时，"构建 + 测试"都必须是绿的。**
> 不能自动验证的改动不算完成 —— 这是"工程化"和"能跑就行"的分界线。

## 阶段 0：地基（✅ 已完成）

| 项 | 内容 | 验收 |
|----|------|------|
| 构建 | CMake + Ninja + MSVC，FFmpeg/SDL2 走 imported target | `tools/build.bat` 零 error 零 warning |
| 分层 | core / media / output / output-sdl / player / ui / app | 依赖方向被构建系统强制（app 想直接调 FFmpeg 会编译不过） |
| 依赖隔离 | FFmpeg 只在 `media` 层出现；SDL 只在 `output/sdl` 出现 | `rg '#include <libav' src --glob '!src/media/**'` 无结果 |
| 运行期 DLL | 构建后自动拷到 `build/bin` | exe 双击即可跑 |

## 阶段 1：播放内核可验证（✅ 已完成）

| 项 | 内容 | 验收 |
|----|------|------|
| 会话与线程 | demux / audio / video 三条后台线程 + 主线程 Tick | 播放中主线程不再被阻塞（旧的"窗口无响应"根因） |
| 主时钟 | 音频主时钟 + 墙钟兜底，倍速只改一处折算公式 | `MediaClock_*` 5 项测试 |
| 队列三语义 | `Close` / `Abort` / `Reset` + 可丢弃前缀，seek 时不再漏包/卡死 | `BoundedQueue_*` 6 项测试 |
| 后端可替换 | `IBackend` + 注册表 + null 后端 | `BackendRegistry_*` / `Null*` 6 项测试 |
| 空后端端到端 | 无窗口/无声卡也能跑真视频 | `Player_*` 4 项 + `Media_*` 4 项测试 |

## 阶段 2：交互（✅ 已完成）

| 项 | 内容 | 验收 |
|----|------|------|
| 覆盖层 | 位图字体 + 进度条 + 自动隐藏，纯逻辑与绘制分离 | `Overlay_*` 9 项 + `BitmapFont_*` 3 项 |
| 键鼠控制 | 空格/单击=暂停，←→=∓5s，↑↓=∓60s，`[`/`]`/滚轮=倍速，拖进度条=跳转 | `交互_*` 3 项（走完整链路：事件→控制器→Action→Player，含暂停预览） |
| 暂停可重绘 | 暂停时输入仍能刷新画面（否则拖进度条看不见） | 代码 + 手工验证 |
| **可中断的背压** | 生产者不再无限阻塞在队列上；暂停预览只丢目标前的音频包，避免音频队列卡住 demux | `交互_预览/拖拽` 回归 + `REVIEW_ROUND2.md` R2-02 |
| 收尾语义 | 音频比视频短时也能正常进 Ended | `Player_播到结尾会进入Ended` |

## 阶段 3：Qt/跨平台宿主（规划中，接口已就位）

| 项 | 内容 | 验收 |
|----|------|------|
| `output/qt` 后端 | `QAudioSink` + `QWidget`/`QOpenGLWidget` 实现 `IAudioSink`/`IVideoSink`/`IEventSource` | 与 SDL 后端**不改 Player 一行**互换 |
| Qt 宿主 | `QTimer` 驱动 `Player::Tick()`；信号桥接 `PlayerObserver` | 见 `QT_INTEGRATION.md` |
| 覆盖层 | 直接用 `QPainter::drawText` 替换位图字体（`OverlayState` 不用改） | 视觉一致 |
| Linux | ALSA/PipeWire 音频 + X11/Wayland 窗口（`output/linux`） | 见 `PLATFORMS.md` |

## 阶段 4：可选增强（不做也不影响架构）

- 字幕（`ISubtitleDecoder` + 在预览层合成）
- 硬件解码（`AVHWDeviceContext` 只在 media 层内部）
- 无缝循环（demux 线程内 `seek(0)`，扩展点已留）
- A/V 偏差统计面板（`PlayerStats` 已有 `lastVideoPts` / 丢帧数）

## 验收命令（每次改动都跑）

```powershell
F:\project\audio_vioce\avplayer\tools\build.bat           # 构建（含警告检查）
F:\project\audio_vioce\avplayer\build\bin\av_tests.exe    # 40 项测试
```

> `tools/build.bat` 会检测项目头文件是否比上次成功构建更新；若是，会先清理旧对象再构建。
> 这是为了规避中文 MSVC + CMake/Ninja 的本地化 `/showIncludes` 前缀问题，详见 `REVIEW_ROUND2.md` R2-07。

带窗口的手工验收：

```powershell
$exe = 'F:\project\audio_vioce\avplayer\build\bin\av_console.exe'
$mp4 = 'F:\project\audio_vioce\1-zzitai-480P-AVC 00_00_00-00_00_05.mp4'
& $exe $mp4 --backend sdl
```

检查清单：播放中**拖拽窗口边框/最小化/最大化不卡死**；空格暂停后**画面与进度条仍能更新**；
拖动进度条**松手才跳**；`[`/`]` 改倍速后**声音变调、画面同步**（磁带式变速）。
