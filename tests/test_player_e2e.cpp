// 端到端测试：用 null 后端把"整套播放逻辑"跑起来（无窗口、无声卡）。
// 这就是"后端可空"这一设计带来的回报：CI 里能自动验证 seek / 倍速 / 结束。
#include "TestHarness.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "av/Player.h"
#include "av/output/BackendRegistry.h"
#include "av/output/NullBackend.h"

using namespace av;

namespace {

using namespace std::chrono_literals;

std::string SamplePath()
{
#if defined(AVPLAYER_TEST_MEDIA_DIR)
    const std::filesystem::path dir{ AVPLAYER_TEST_MEDIA_DIR };
    const std::filesystem::path file = dir / "1-zzitai-480P-AVC 00_00_00-00_00_05.mp4";
    if (std::filesystem::exists(file)) return file.string();
#endif
    return {};
}

// 驱动 Player 跑一小段真实时间（宿主循环的迷你版）
bool TickFor(av::Player& player, double seconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        player.Tick();
        if (player.QuitRequested()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

bool TickUntil(av::Player& player, std::function<bool()> predicate, double timeoutSeconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        player.Tick();
        if (predicate()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

std::shared_ptr<av::output::IBackend> MakeNullBackend()
{
    // 注册是宿主的职责（app 决定用哪个后端）；测试里也一样，先注册再取。
    av::output::RegisterNullBackend();
    return av::output::BackendRegistry::Instance().Create("null");
}

} // namespace

AV_TEST(Player_打开探测_播放_跳转_倍速)
{
    const std::string sample = SamplePath();
    if (sample.empty())
    {
        std::cout << "         (跳过：找不到样例视频，设置 -DAVPLAYER_TEST_MEDIA_DIR=<目录>)\n";
        return;
    }

    core::PlayerConfig config;
    av::Player player(MakeNullBackend(), config);

    AV_CHECK(player.Open(sample).ok());
    AV_CHECK(player.IsOpen());
    AV_CHECK(player.Media().hasVideo);
    AV_CHECK(player.Media().hasAudio);
    AV_CHECK_NEAR(player.Media().durationSeconds, 5.0, 0.5);

    AV_CHECK(player.Play().ok());
    AV_CHECK(player.State() == av::PlayerState::Playing);
    // 视频输出的可绘制区尺寸可以被宿主查询（覆盖层几何要按它算）
    // RenderSize 查询先注释掉，用来定位崩溃

    // 1) 正常播放：应当有帧被呈现，位置在前进
    AV_CHECK(TickUntil(player, [&] { return player.Stats().presentedFrames > 5; }, 3.0));
    AV_CHECK(player.Position() > 0.05);

    // 2) 暂停：位置不再前进
    player.Pause();
    AV_CHECK(player.State() == av::PlayerState::Paused);
    const double pausedPosition = player.Position();
    TickFor(player, 0.3);
    AV_CHECK_NEAR(player.Position(), pausedPosition, 0.05);

    // 3) 跳转：跳到 3 秒，画面应当很快跟上
    player.Play();
    player.SeekTo(3.0);
    AV_CHECK(TickUntil(player, [&] { return player.Stats().lastVideoPts >= 3.0; }, 4.0));
    AV_CHECK(player.Stats().lastVideoPts >= 2.9);

    // 4) 倍速：2 倍速下媒体时间应当跑得明显更快
    player.SetSpeed(2.0);
    AV_CHECK_NEAR(player.Speed(), 2.0, 1e-9);

    const double before = player.Position();
    const auto   start  = std::chrono::steady_clock::now();
    TickFor(player, 1.0);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double advanced = player.Position() - before;
    AV_CHECK(advanced > 0.0);
    AV_CHECK(advanced / elapsed > 1.3);      // 明显快于 1 倍速（留足容差，避免 CI 抖动）

    player.RequestQuit();
    AV_CHECK(player.QuitRequested());
    player.Close();
    AV_CHECK(player.State() == av::PlayerState::Idle);
}

AV_TEST(Player_播到结尾会进入Ended)
{
    const std::string sample = SamplePath();
    if (sample.empty())
    {
        std::cout << "         (跳过：找不到样例视频)\n";
        return;
    }

    core::PlayerConfig config;
    av::Player player(MakeNullBackend(), config);
    AV_CHECK(player.Open(sample).ok());
    AV_CHECK(player.Play().ok());

    player.SetSpeed(4.0);             // 4 倍速：5 秒的片子约 1.3 秒播完
    const bool ended = TickUntil(player, [&] { return player.State() == av::PlayerState::Ended; }, 6.0);
    AV_CHECK(ended);
    AV_CHECK(player.Stats().presentedFrames > 20);

    player.Close();
}

AV_TEST(Player_没有后端时Open会失败)
{
    av::Player player(nullptr);
    const core::Status status = player.Open("whatever.mp4");
    AV_CHECK(!status.ok());
    AV_CHECK(player.State() == av::PlayerState::Error);
}

// SetSpeed() 在 Open() 之前调用也不能"吞掉"：
// 界面（比如按配置起播就 2 倍速）习惯先设参数再打开文件。
AV_TEST(Player_Open之前设置倍速也生效)
{
    const std::string sample = SamplePath();
    if (sample.empty())
    {
        std::cout << "         (跳过：找不到样例视频)\n";
        return;
    }

    av::Player player(MakeNullBackend());
    player.SetSpeed(2.0);
    AV_CHECK_NEAR(player.Speed(), 2.0, 1e-9);

    AV_CHECK(player.Open(sample).ok());
    AV_CHECK(player.Play().ok());
    AV_CHECK_NEAR(player.Speed(), 2.0, 1e-9);

    // 时钟也要真的按 2 倍推进（而不只是记了个数）
    const double before = player.Position();
    const auto   start  = std::chrono::steady_clock::now();
    TickUntil(player, [&] { return player.Position() - before > 1.0; }, 1.5);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    AV_CHECK(player.Position() - before > 1.0);
    AV_CHECK(elapsed < 1.4);            // 1 秒的媒体时间只花了不到 1.4 秒墙钟

    player.Close();
}
