# 学习指南：以本工程为例，怎么把"能跑"做成"工程"

这份文档不讲 API，讲**决策**。每一条都对应本仓库里的真实代码，以及我们真踩过的坑。

> 两轮审查的完整记录见 [`REVIEW_ROUND1.md`](REVIEW_ROUND1.md) 与 [`REVIEW_ROUND2.md`](REVIEW_ROUND2.md)。

---

## 一、先找"变化轴"，再谈架构

新手常见做法：看到需求就加一个 `if`，或提前抽象一堆用不上的接口。
工程做法：**先列出"未来会变的东西"，架构只围绕它们做隔离。**

本工程列出 4 条变化轴，每一条都有明确的隔离点：

| 变化轴 | 会变成什么 | 隔离点（改动范围） |
|--------|------------|--------------------|
| 输出后端 | SDL → Qt → ALSA → 假后端 | `output/*`（`IBackend` 实现） |
| 媒体格式/编解码 | 换 FFmpeg 版本、上加硬解、换库 | `media/*`（适配器层） |
| 宿主框架 | 命令行 → Qt → 嵌入式 | app（`Tick` 的驱动者 + 胶水 20 行） |
| 交互方案 | 键位、鼠标、进度条样式 | `ui/*`（`InputMapConfig` + `OverlayState`） |

**没有变化的轴不要去抽象。** 例如"音频重采样用 swr"就是不变的部分，
所以 `AudioResampler` 是具体类而不是接口 —— 抽象要留给会变的东西。

> 练习：拿到任何需求，先问"这属于哪条变化轴？"，答案会直接告诉你该改哪个目录。

---

## 二、依赖方向：让编译器替你守纪律

规则：**依赖只能从上往下，不能反过来。**

```
app ──► ui ──► player ──► output ──► (空实现) 
                 │           │
                 ▼           ▼
               media ──► core ◄── output/ui
```

关键是这条规则**不靠自觉**，靠构建系统强制：

```cmake
# 只有 media 能看见 FFmpeg
target_link_libraries(avmedia PUBLIC av::core
                              PUBLIC FFmpeg::avutil FFmpeg::avcodec ...)
# player 只是"私有地用" media：app 想直接调 FFmpeg 就得自己加依赖 => 摩擦即约束
target_link_libraries(avplayer PUBLIC av::core av::output PRIVATE av::media)
```

于是"app 里偷偷 `#include <libavcodec/avcodec.h>`"会**编译不过**。
比写十条代码规范有用得多。

同一条规则还体现在**头文件内容**上：`Player.h` 里没有任何 FFmpeg / SDL 类型
（实现全在 `Player.cpp` 的 pimpl 里）。这叫"依赖倒置的物理化"。

---

## 三、跨层传递：传"视图"，不传"所有权"

`AVFrame` 是 FFmpeg 的类型，能不能出现在 `output` 层？**不能**，
否则换掉 FFmpeg 就得改所有后端。

但数据必须传给后端。做法是"只读视图 + 所有权仍在上层"：

```cpp
struct VideoFrameView            // core 层定义：std::uint8_t* planes[4]; int strides[4]; ...
{
    const std::uint8_t* planes[4]{};
    int                 strides[4]{};
    core::VideoFormat   format;
    double              ptsSeconds = 0.0;
};
```

好处有三：
1. **零拷贝**（只传指针）；
2. 后端完全不知道 FFmpeg 的存在；
3. 后端**不能**持有/释放这块内存 —— 所有权没给它，想出错都难。

> 反面教材：把 `AVFrame*` 传进后端，于是后端的生命周期、`av_frame_unref` 时机、
> 跨线程访问全成了口头约定。

---

## 四、门面 + pimpl：对外只暴露"契约"

```cpp
class Player                       // 对外：一个纯粹的契约
{
public:
    explicit Player(std::shared_ptr<output::IBackend> backend, const core::PlayerConfig& config = {});
    core::Status Open(const std::string& url);
    void         Tick();           // 宿主每帧调用
    ...
private:
    struct Impl;                   // 全部实现细节（FFmpeg/SDL/线程）关在这里
    std::unique_ptr<Impl> impl_;
};
```

收益：
- **编译防火墙**：改 `Player.cpp` 不会让 app 重编译；
- **依赖可控**：第三方类型不会从私有头文件"漏"到公开接口；
- **ABI/接口稳定**：加成员不破坏调用方。

代价：多一层间接、调试时多一层。**当且仅当"有第三方依赖或稳定接口需求"时才值得。**

---

## 五、最决定性的一条：`Tick()` 由宿主驱动

```cpp
// 控制台宿主
while (!quit) { player.Tick(); hud.Tick(dt); sleep(2ms); }
// Qt 宿主
connect(&timer, &QTimer::timeout, this, [&]{ player.Tick(); });
```

对比"播放器自带一个线程循环 + 回调"的老写法，宿主驱动带来：

1. **UI 框架的线程约束自动满足**：Qt/任意 GUI 都要求窗口操作在主线程，
   而 `Tick()` 天然在主线程 —— 不需要跨线程 marshal；
2. **"窗口无响应"从根上消失**：事件泵（`IEventSource::Poll`）就在 `Tick()` 里，
   主线程每 2ms 泵一次消息；
3. **结束/异常/重入可控**：宿主要退出就停止调用 `Tick()`，没有"后台循环还在跑"的悬案；
4. **可测试**：测试里 `while (Tick()) ...` 就能驱动真实播放（见 `test_player_e2e.cpp`）。

> 这条设计本身不花多少代码，但它决定了后面所有事情是否顺手。
> **架构的价值往往就藏在这种"一个决定换掉一类麻烦"的地方。**

---

## 六、阻塞原语必须可被打断（本次真踩的坑）

`BoundedQueue::Push` 原本是"队列满就一直等"。测试里发现：
**暂停后拖进度条，什么都不发生。**

追下来是死锁：

```
主线程（暂停中，不再取帧）
   ↑ 等 videoFrames.Take
视频线程：卡在 videoFrames.Push（满了，无限等）
   ↑ 等 videoPackets.Take
demux 线程：卡在 videoPackets.Push（满了，无限等）
   ↑ 而 seek 请求要由 demux 线程执行 ⇒ 谁都动不了
```

修法：加 `PushWaiting(value, timeout)` —— 「等一小会儿就回来看看控制位」。

```cpp
while (!state.videoFrames.PushWaiting(tagged, 10ms))
{
    if (state.aborting.load() || state.quitRequested.load()) return;
    if (state.CurrentGeneration() != generation) return;   // 世代变了就别再灌数据
}
```

教训：**在"生产者-消费者 + 控制指令"的系统里，无限阻塞就是设计缺陷**，
不管它看起来多"优雅"。任何可能无限等待的地方，都要能回答：
"如果这时来了暂停/跳转/退出，它怎么知道？"

第二轮又发现了一层更隐蔽的背压：暂停预览时，音频线程合法地不再写设备，`audioPackets` 很快被 demux 填满，demux 随即卡住，视频预览也拿不到后面的包。`PushWaiting` 只能让 demux 看见 seek 请求，不能凭空制造音频消费者。

最终规则是：普通暂停保持队列；**只有暂停 seek 预览期间**允许丢弃 `pts + duration <= seekTarget` 的音频包，目标及之后的包保留。这体现了一个更一般的判断：

> 不是所有"消费者暂停"都等于"生产者应该无限等待"；要区分短暂控制信号、长期停顿和可丢弃数据。

---

## 七、RAII 包装的两条铁律（也是真踩的坑）

`Packet` / `Frame` 包装了 `AVPacket*` / `AVFrame*`。踩了两个坑：

**坑 1：被 `std::move` 走之后的对象不能再拿去调用 C API。**
```cpp
media::Packet packet;
...
state.videoPackets.Push(std::move(packet));   // packet 内部指针已变成 nullptr
... 下一轮循环又 av_read_frame(fmt, packet.Raw());   // 💥 空指针崩溃
```
修法：**一包一对象**（循环内构造），并在 `Demuxer::Read` 里加防御：
传进来没分配的 `Packet` 就明确报错，而不是等 FFmpeg 段错误。

**坑 2：默认构造不能有"看起来像有数据"的副作用。**
```cpp
explicit Frame(core::Rational timeBase = core::kSeconds);   // 默认构造就 av_frame_alloc()
...
core::Tagged<media::Frame> pendingFrame;
pendingFrame = {};                       // 想表达"没有待呈现的帧"
if (!pendingFrame.value.IsAllocated())   // ❌ 永远是 true：空帧也是"已分配"
```
后果极其隐蔽：播放器**一直在用空帧**（pts=0），表现为疯狂丢帧、播不到结尾。
修法：把"有容器"和"有数据"分成两个概念：

```cpp
Frame() = default;                    // 空帧，不分配
explicit Frame(core::Rational tb);    // 要接解码结果时才分配
bool IsAllocated() const;             // 容器存在
bool HasData() const;                 // 真有数据（data[0] != nullptr）
```

教训：
- **哨兵对象要便宜且无副作用**（能不能不用 `optional`？这里用 `Tagged<T>` 就得保证 T 的默认值语义干净）；
- 断言要断言"我真正想表达的东西"：想判断"有没有帧"就写 `HasData()`，别用近义词顶替。

---

## 八、时间：只能有一个权威

A/V 同步最怕"到处算时间"。本工程的做法：

```cpp
// core/MediaClock：全套代码里唯一的"现在几点了"
音频主时钟： 媒体时间 = 最后写入设备的音频 pts − 排队字节 × speed / 字节率
墙钟兜底：   没有音频流时，主线程 TickWall() 推进
```

倍速只在这一个公式里体现（乘 `speed`），于是：
- 视频侧同步逻辑一个字不用改；
- 变速后"声音会变调、画面跟着走"自动成立（磁带式变速）。

推论：**"时间基准"和"数据流"必须解耦**。谁都不许自己攒一个 `steady_clock` 去猜位置。

---

## 九、把"可测试性"当设计指标

本工程 40 项测试全部 headless（无窗口、无声卡、可进 CI），靠的都是设计而非技巧：

| 测试需要的能力 | 设计上怎么给的 |
|----------------|----------------|
| 不开窗口就能跑播放 | `null` 后端（`IBackend` 的第二个实现） |
| 能断言"画在哪个像素" | `IRenderTarget` 只要求矩形/线段原语 → 测试里用录制器 |
| 能脚本化输入 | `IEventSource` 可替换，`NullEventSource` 支持 `Push()` |
| 能测"暂停后位置不动" | `MediaClock` 支持注入 `IClock`（`ManualClock`） |
| 能测 UI 逻辑而不建窗口 | `OverlayState` 是纯数据，`OverlayController` 是纯逻辑 |

**"这个模块怎么测？"如果答不上来，通常说明它和环境耦合太深** —— 那就先解耦，再写测试。

本次新加的 `test_player_input.cpp` 更是直接验证"按键/拖拽真的能控制播放器"，
它能在**没有窗口的 CI 里**复现"暂停 + 拖进度条死锁"这种交互 bug。

---

## 十、边界要"收口"，不要"到处兼容"

这个 SDK 的 FFmpeg 头文件**没有 `extern "C"` 保护**（C++ 里会把 `av_*` 当 C++ 函数 mangle，
链接期必然 LNK2019）。修法不是"每个 .cpp 里加一遍"，而是**设一个唯一的门**：

```cpp
// av/media/FFmpegCompat.h —— 全项目唯一允许出现 FFmpeg 头的地方
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
...
}
```

配套纪律：其他文件一律只 `#include "av/media/FFmpegCompat.h"`。
换 SDK、换 FFmpeg 大版本（头文件改名/API 变动）都只改这一个文件。

**通用原则：所有"第三方怪癖"都收口到一个文件里，别让它渗进业务代码。**

---

## 十一、工程化细节（看起来小，影响很大）

- **编码**：源码 UTF-8 无 BOM + LF（Windows 上 CRLF/GBK 混用是 MSVC 警告与乱码的源头）。
- **警告即错误**：`/W4 /permissive-`，第三方头用 `/external:W0` 隔离 —— 自己的代码必须零警告。
- **构建脚本**：`tools/build.bat` 固定工具链与环境变量。中文 MSVC 会把
  `/showIncludes` 前缀打印成本地化文本，而 CMake/Ninja 可能把该前缀的编码保存错，
  结果是**头文件依赖全丢**，甚至不同翻译单元使用不同的对象布局。`VSLANG=1033`
  在这台机器上并不能可靠解决，因此脚本改为用 `tools/check_header_changes.ps1`
  检测头文件更新时间；发生变化就先 `clean`，成功构建后更新 stamp。
- **日志分级**：`Trace/Debug/Info/Warn/Error`，默认 Info；
  诊断信息（丢帧、呈现 pts）放 Debug，需要时 `--log debug` 一把看清。
- **文档与代码同源**：`docs/` 里的每一条设计说明都能在代码里找到对应注释，
  代码注释里也直接写"为什么"（踩过的坑、取舍），而不是复述"做了什么"。

---

## 十二、面对新需求时的五问

1. **落在哪条变化轴？** → 决定改哪个目录（多数需求只改一层）。
2. **会不会破坏依赖方向？** → 会不会让 `core/ui` 里出现第三方类型？
3. **怎么证明它对了？** → 能不能写一个 headless 测试？不能的话，是不是耦合太深？
4. **异常路径想清楚了吗？** → 空指针 / 队列满 / 超时 / 中断 / 提前 EOF / 设备失败。
   （本工程 5 个真 bug 里 4 个都在异常路径上。）
5. **命名表达了意图吗？** → `HasData()` vs `IsAllocated()`、`PushWaiting` vs `Push`、
   `Close/Abort/Reset` 三种结束语义必须一眼可辨。

---

## 十三、建议的下一步练习

1. **加一个后端**：写 `output/qt`（照 `QT_INTEGRATION.md`），跑通后再写一个假的"录音后端"。
   目标：体会"零改动换输出"到底是什么感觉。
2. **加一个交互**：实现"鼠标悬停进度条时显示时间气泡"。
   只改 `ui`（状态 + 绘制），不改 `player`。
3. **加一个字幕**：`media` 里加 `SubtitleDecoder`，`ui` 里加绘制。
   目标：体会"新数据流"怎么接入已有的 pipeline。
4. **做性能对照**：把 `VideoConverter`（sws 软转）与后端原生 YUV 上屏对比，
   统计每帧耗时与丢帧数 —— 学会用数据而不是感觉来优化。
5. **故意引入一个 bug**（比如去掉 `dropBefore` 过滤），看测试能不能抓到。
   如果抓不到，说明测试的"可观测面"还不够 —— 那就先补观测点，再谈修 bug。

---

## 十四、第二轮审查补充：抽象不能抹掉关键语义

本轮最典型的一个真实缺陷是 AAC 的 `FLTP`（planar float）被抽象成了
`core::SampleFormat::F32`，而 `F32` 在 FFmpeg 边界又被映射回 packed `FLT`。
结果 `swr_convert` 按 packed 布局读取 planar 帧，起播即越界崩溃。

这里的问题不是"FFmpeg 难用"，而是**抽象层丢掉了对调用方有意义的区别**：

- 对 UI/时钟来说，"F32"足够；
- 对实际读取内存的 resampler 来说，packed/planar 是必须保留的 ABI 级信息。

修法不是把 `AVSampleFormat` 泄漏到 `core`，而是让 `AudioResampler` 在自己的 FFmpeg 边界内保留真实格式：

1. 配置时先用中性格式建立初值；
2. 首帧到达时以 `AVFrame::format` 为准；
3. 只有格式真的变化才重建 `SwrContext`；
4. 用单测构造左右声道为 `+1/-1` 的 planar 帧，验证输出仍是 `+1/-1` 而不是两个声道相同。

可迁移的工程原则：

> **抽象要隐藏实现，但不能隐藏影响正确性的语义。**  
> 判断标准不是"类型看起来干不干净"，而是"拿到这个抽象的人能否正确完成自己的工作"。

另一个同轮例子是 NV12：它虽然只有一个像素格式枚举值，却需要两个平面指针。`MakeVideoView`/`VideoConverter` 只填一个平面时，语义已经在接口边界被抹掉，后端自然无法正确上传纹理。修复方式同样是让"中性视图"保留足够的信息。

最后，构建系统也是正确性的一部分：本次 `SharedState.h` 改动后没有重建全部对象，导致不同编译单元对同一个结构体使用不同布局，调试器里甚至看到 `generation = 0xCC...`。这说明**"代码正确"不等于"产物正确"**；依赖图、工具链本地化和构建缓存都属于架构可靠性的一部分。

建议读者按这个顺序复盘本工程：

1. 先看 `REVIEW_ROUND1.md`，理解为什么先冻结接口；
2. 再看 `REVIEW_ROUND2.md`，看每个冻结项如何被实现和证伪；
3. 最后读 `Player.cpp`、`VideoPipeline.cpp`、`AudioPipeline.cpp` 的注释，把"注释中的为什么"和测试对应起来。
