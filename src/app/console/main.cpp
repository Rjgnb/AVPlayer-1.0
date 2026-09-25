// ===========================================================================
// 控制台宿主：演示"宿主怎么驱动 Player"
//
//   1) 选后端（app 是唯一决定"用哪个后端"的地方）
//   2) 建 Player，注册观察者 + 覆盖层绘制钩子
//   3) while (!quit) { player.Tick(); hud.Tick(dt); sleep(2ms); }
//
// 换成 Qt 时，这个 while 循环会被 QTimer 取代，其余一模一样。
// ===========================================================================
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "ConsoleHud.h"

#include "av/Player.h"
#include "av/core/Log.h"
#include "av/output/BackendRegistry.h"
#include "av/output/NullBackend.h"

#if defined(AVPLAYER_HAS_SDL)
#include "av/output/sdl/SdlBackend.h"
#endif

// app 是最上面一层：这里用 using 换短一点的写法，不影响任何库代码的可读性
// （库里的头文件一律不写 using —— 那会污染所有包含它的人）。
using namespace av;

namespace {

std::atomic<bool> g_interrupted{ false };

void HandleSignal(int) { g_interrupted = true; }

struct Options
{
    std::string url;
    std::string backend;
    std::string logLevel = "info";
    double speed = 1.0;
    double start = 0.0;
    double runSeconds = 0.0;     // >0：跑这么久就退出（无人值守/自测）
    bool showVideo = true;
    bool showAudio = true;
    bool listBackends = false;
    bool help = false;
};

void PrintUsage()
{
    std::cout <<
        "用法: av_console <媒体文件> [选项]\n"
        "  --backend <名字>     输出后端（默认：注册表里的第一个；sdl / null）\n"
        "  --speed <倍速>       0.25 ~ 4.0（默认 1.0）\n"
        "  --start <秒>         起始位置\n"
        "  --run-seconds <秒>   播放这么久后自动退出（用于自测/回归）\n"
        "  --no-video / --no-audio\n"
        "  --log <trace|debug|info|warn|error>\n"
        "  --list-backends      列出已注册的后端后退出\n"
        "  --help\n"
        "\n"
        "播放中的操作：空格/单击画面 = 暂停；← → = ∓5s；↑ ↓ = ∓60s；\n"
        "              [ ] 或滚轮 = 倍速；拖拽进度条 = 跳转；Esc = 退出\n";
}

bool ParseOptions(int argc, char** argv, Options& options, std::string& error)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 >= argc) { error = arg + " 需要参数"; return false; }
            out = argv[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h") { options.help = true; continue; }
        if (arg == "--list-backends") { options.listBackends = true; continue; }
        if (arg == "--no-video") { options.showVideo = false; continue; }
        if (arg == "--no-audio") { options.showAudio = false; continue; }

        std::string value;
        if (arg == "--backend") { if (!next(value)) return false; options.backend = value; continue; }
        if (arg == "--log")     { if (!next(value)) return false; options.logLevel = value; continue; }
        if (arg == "--speed")   { if (!next(value)) return false; options.speed = std::atof(value.c_str()); continue; }
        if (arg == "--start")   { if (!next(value)) return false; options.start = std::atof(value.c_str()); continue; }
        if (arg == "--run-seconds") { if (!next(value)) return false; options.runSeconds = std::atof(value.c_str()); continue; }

        if (!arg.empty() && arg[0] == '-')
        {
            error = "未知选项: " + arg;
            return false;
        }
        if (!options.url.empty())
        {
            error = "只能指定一个媒体文件";
            return false;
        }
        options.url = arg;
    }
    return true;
}


} // namespace

int main(int argc, char** argv)
{
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error))
    {
        std::cerr << "参数错误: " << error << "\n\n";
        PrintUsage();
        return 2;
    }
    if (options.help) { PrintUsage(); return 0; }

    // ---- 日志 ----
    core::LogLevel level = core::LogLevel::Info;
    if (!core::ParseLogLevel(options.logLevel, level))
    {
        std::cerr << "未知日志级别: " << options.logLevel << "\n";
        return 2;
    }
    core::Logger::Instance().SetLevel(level);

    // ---- 注册后端（这里是唯一"知道具体后端"的地方）----
    // 默认后端 = 第一个注册成功的那一个（BackendRegistry 的规则）：
    // 有 SDL 就用 SDL，没有就退化成 null。想指定就加 --backend。
#if defined(AVPLAYER_HAS_SDL)
    av::output::sdl::RegisterBackend();
#endif
    av::output::RegisterNullBackend();

    if (options.listBackends)
    {
        std::cout << "可用后端: ";
        for (const std::string& name : av::output::BackendRegistry::Instance().Names()) std::cout << name << " ";
        std::cout << "\n";
        return 0;
    }
    if (options.url.empty())
    {
        PrintUsage();
        return 2;
    }

    // ---- 组合：后端 -> Player ----
    std::shared_ptr<av::output::IBackend> backend =
        av::output::BackendRegistry::Instance().Create(options.backend);
    if (!backend)
    {
        std::cerr << "没有找到可用的后端（可用: ";
        for (const std::string& name : av::output::BackendRegistry::Instance().Names()) std::cerr << name << " ";
        std::cerr << "）\n";
        return 3;
    }

    core::PlayerConfig config;
    config.enableVideo = options.showVideo;
    config.enableAudio = options.showAudio;
    config.startSeconds = options.start;
    config.logLevel = level;
    config.windowTitle = options.url;

    const core::Status initStatus = backend->Initialize(config.backendOptions);
    if (!initStatus.ok())
    {
        std::cerr << "后端初始化失败: " << initStatus.ToString() << "\n";
        return 3;
    }

    av::Player player(backend, config);
    // HUD 既是观察者（把状态映射到界面状态）也是覆盖层绘制者：
    // 一个观察者接口就够用了 —— 想拆成"日志观察者 + UI 观察者"，
    // 加一个 PlayerObserver 转发器即可，不必让 Player 支持多个观察者。
    app::ConsoleHud hud(player);

    std::signal(SIGINT, HandleSignal);

    const core::Status openStatus = player.Open(options.url);
    if (!openStatus.ok())
    {
        std::cerr << "打开失败: " << openStatus.ToString() << "\n";
        return 4;
    }

    if (options.speed != 1.0) player.SetSpeed(options.speed);

    const core::Status playStatus = player.Play();
    if (!playStatus.ok())
    {
        std::cerr << "播放失败: " << playStatus.ToString() << "\n";
        return 5;
    }

    const auto startTime = std::chrono::steady_clock::now();
    auto previous = startTime;

    while (!player.QuitRequested() && !g_interrupted.load())
    {
        player.Tick();

        const auto now = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double>(now - previous).count();
        previous = now;
        hud.Tick(delta);

        if (options.runSeconds > 0.0 &&
            std::chrono::duration<double>(now - startTime).count() >= options.runSeconds)
        {
            break;
        }

        // 这里就是"宿主自带的主循环"：2ms 一次，足够跟手，又不烧 CPU。
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const av::PlayerStats stats = player.Stats();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    std::cout << "\n---- 统计 ----\n"
              << "运行 " << elapsed << "s，位置 " << core::FormatTimecode(player.Position())
              << "，呈现帧 " << stats.presentedFrames << "，丢帧 " << stats.droppedFrames
              << "，最后视频 pts " << stats.lastVideoPts << "\n";

    player.Close();
    backend->Shutdown();
    return 0;
}
