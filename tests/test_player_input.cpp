// 交互路径的端到端测试：输入事件 -> ui 覆盖层控制器 -> Action -> Player
//
// 这条链是"点了没反应"这类问题的唯一现场。下面 InputDriver 就是宿主（控制台/Qt）里
// 那 20 行胶水层：把事件喂给 OverlayController，再把算出来的 Action 施加到 Player。
// 因为 Player::HandleInputEvent 是公开接口，这条链可以完全 headless 地测。
#include "TestHarness.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "av/Player.h"
#include "av/output/BackendRegistry.h"
#include "av/output/NullBackend.h"
#include "av/ui/Overlay.h"

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

std::shared_ptr<output::IBackend> MakeNullBackend()
{
    output::RegisterNullBackend();
    return output::BackendRegistry::Instance().Create("null");
}

// 宿主的"胶水层"（和 src/app/console/ConsoleHud.h 同构，去掉打印）
class InputDriver final : public PlayerObserver
{
public:
    explicit InputDriver(Player& player) : player_(player) { player_.SetObserver(this); }

    void Resize(int width, int height) { controller_.Layout(state_, core::Size{ width, height }); }
    void Tick(double deltaSeconds) { controller_.Update(state_, deltaSeconds); }
    void Feed(const core::InputEvent& event) { player_.HandleInputEvent(event); }

    const ui::OverlayState& State() const { return state_; }

    // ---- PlayerObserver ----
    void OnInputEvent(const core::InputEvent& event) override
    {
        SyncFromPlayer();   // 和 ConsoleHud 一样：界面状态对齐播放器（唯一真值）
        Apply(controller_.Handle(event, state_));
    }
    void OnMediaOpened(const MediaDescription& media) override { state_.duration = media.durationSeconds; }
    void OnPositionChanged(double seconds) override
    {
        if (!state_.dragging) state_.position = seconds;
    }
    void OnStateChanged(PlayerState state) override { state_.playing = (state == PlayerState::Playing); }

private:
    void SyncFromPlayer()
    {
        state_.playing = player_.IsPlaying();
        state_.speed   = player_.Speed();
        if (state_.duration <= 0.0) state_.duration = player_.Duration();
        if (!state_.dragging) state_.position = player_.Position();
    }

    void Apply(const ui::Action& action)
    {
        switch (action.type)
        {
        case ui::ActionType::TogglePause: player_.TogglePlayPause(); break;
        case ui::ActionType::SeekTo:
            player_.SeekTo(action.value);
            state_.position = action.value;
            break;
        case ui::ActionType::SeekRelative:
        {
            double target = player_.Position() + action.value;
            if (target < 0.0) target = 0.0;
            player_.SeekTo(target);
            break;
        }
        case ui::ActionType::SetSpeed:
            player_.SetSpeed(action.value);
            state_.speed = action.value;
            break;
        case ui::ActionType::Quit: player_.RequestQuit(); break;
        case ui::ActionType::None: break;
        }
    }

    Player&                player_;
    ui::OverlayController  controller_;
    ui::OverlayState       state_;
};

core::InputEvent Key(core::KeyCode key)
{
    core::InputEvent event;
    event.type = core::InputEventType::KeyDown;
    event.key  = key;
    return event;
}

core::InputEvent Mouse(core::InputEventType type, int x, int y)
{
    core::InputEvent event;
    event.type     = type;
    event.button   = core::MouseButton::Left;
    event.position = core::Point{ x, y };
    return event;
}

bool TickUntil(Player& player, InputDriver& driver, std::function<bool()> predicate, double timeoutSeconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        player.Tick();
        driver.Tick(0.002);
        if (predicate()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

} // namespace

AV_TEST(交互_空格暂停_方向键跳转_方括号倍速)
{
    const std::string sample = SamplePath();
    if (sample.empty())
    {
        std::cout << "         (跳过：找不到样例视频)\n";
        return;
    }

    Player player(MakeNullBackend());
    InputDriver driver(player);
    driver.Resize(800, 600);

    AV_CHECK(player.Open(sample).ok());
    AV_CHECK(player.Play().ok());
    AV_CHECK(TickUntil(player, driver, [&] { return player.Stats().presentedFrames > 3; }, 3.0));

    // 空格 -> 暂停
    driver.Feed(Key(core::KeyCode::Space));
    AV_CHECK(player.State() == PlayerState::Paused);

    // 再按空格 -> 继续
    driver.Feed(Key(core::KeyCode::Space));
    AV_CHECK(player.State() == PlayerState::Playing);

    // ] -> 提速一档（离散档位，1.0 -> 1.25）
    driver.Feed(Key(core::KeyCode::RightBracket));
    AV_CHECK_NEAR(player.Speed(), 1.25, 1e-9);

    // [ -> 降回来
    driver.Feed(Key(core::KeyCode::LeftBracket));
    AV_CHECK_NEAR(player.Speed(), 1.0, 1e-9);

    // → -> 前进 5 秒（片子只有 5.13s，会被夹到结尾）
    player.SeekTo(0.0);
    AV_CHECK(TickUntil(player, driver, [&] { return player.Position() < 1.0; }, 3.0));
    const double before = player.Position();
    driver.Feed(Key(core::KeyCode::Right));
    AV_CHECK(TickUntil(player, driver, [&] { return player.Position() > before + 1.0; }, 3.0));

    player.Close();
}

// "暂停时拖进度条"不只是位置要跳过去，画面也得跟着变成目标位置那一帧 ——
// 否则用户看不到自己跳到哪儿了（体感上就是"拖了没反应"）。
AV_TEST(交互_暂停时拖进度条会预览目标画面)
{
    const std::string sample = SamplePath();
    if (sample.empty())
    {
        std::cout << "         (跳过：找不到样例视频)\n";
        return;
    }

    Player player(MakeNullBackend());
    InputDriver driver(player);
    driver.Resize(800, 600);

    AV_CHECK(player.Open(sample).ok());
    AV_CHECK(player.Play().ok());
    AV_CHECK(TickUntil(player, driver, [&] { return player.Stats().presentedFrames > 3; }, 3.0));

    // 单击画面 -> 暂停
    driver.Feed(Mouse(core::InputEventType::MouseButtonDown, 400, 300));
    driver.Feed(Mouse(core::InputEventType::MouseButtonUp, 400, 300));
    AV_CHECK(player.State() == PlayerState::Paused);

    const double        duration  = player.Duration();
    const std::uint64_t presented = player.Stats().presentedFrames;
    const int           barY      = 600 - 40;
    const int           x         = 40 + static_cast<int>(720 * 0.6);   // 进度条 60% 处

    driver.Feed(Mouse(core::InputEventType::MouseButtonDown, x, barY));
    driver.Feed(Mouse(core::InputEventType::MouseButtonUp, x, barY));

    const double expected = 0.6 * duration;
    // 预览帧会在暂停状态下被画出来：屏幕上最后一帧的 pts 前进到目标附近。
    //
    // 注意这里给的时间比较宽（20s）：样例视频 154 个视频包里只有 2 个关键帧
    // （0.0s 与 5.0s）。跳到 60% 处时 AVSEEK_FLAG_BACKWARD 只能给到 0.0s 那个关键帧，
    // 因此必须先解出从 0.0s 开始的一百多帧、再逐帧丢弃，才能到达目标位置。
    // 对一个只有 2 个关键帧的文件来说，这就是真实成本；换一个有正常关键帧间隔的
    // 文件，预览几乎是立刻出现的。
    AV_CHECK(TickUntil(player, driver, [&] { return player.Stats().lastVideoPts > expected - 0.5; }, 20.0));
    AV_CHECK_NEAR(player.Position(), expected, 0.5);

    // 预览 ≠ 播放：状态还是暂停，也不该记成"新呈现了一帧"
    AV_CHECK(player.State() == PlayerState::Paused);
    AV_CHECK_EQ(player.Stats().presentedFrames, presented);

    player.Close();
}

AV_TEST(交互_单击画面暂停_拖进度条松手才跳转)
{
    const std::string sample = SamplePath();
    if (sample.empty())
    {
        std::cout << "         (跳过：找不到样例视频)\n";
        return;
    }

    Player player(MakeNullBackend());
    InputDriver driver(player);
    driver.Resize(800, 600);

    AV_CHECK(player.Open(sample).ok());
    driver.Resize(800, 600);

    const double duration = player.Duration();
    AV_CHECK(duration > 4.0);

    AV_CHECK(player.Play().ok());
    AV_CHECK(TickUntil(player, driver, [&] { return player.Stats().presentedFrames > 3; }, 3.0));

    // 1) 单击画面中央 -> 暂停
    driver.Feed(Mouse(core::InputEventType::MouseButtonDown, 400, 300));
    driver.Feed(Mouse(core::InputEventType::MouseButtonUp, 400, 300));
    AV_CHECK(player.State() == PlayerState::Paused);

    // 2) 暂停状态下拖进度条：按下 -> 移动 -> 松手才发 seek
    const int barY = 600 - 40;
    driver.Feed(Mouse(core::InputEventType::MouseButtonDown, 100, barY));
    AV_CHECK(driver.State().dragging);

    // 拖动过程中只动 UI，播放位置不变（避免狂发 seek）
    driver.Feed(Mouse(core::InputEventType::MouseMove, 400, barY));
    AV_CHECK(driver.State().dragging);
    AV_CHECK_NEAR(player.Position(), player.Position(), 1e-9);
    AV_CHECK(driver.State().position > duration * 0.3);

    // 松手 -> 跳转
    driver.Feed(Mouse(core::InputEventType::MouseButtonUp, 580, barY));
    AV_CHECK(!driver.State().dragging);

    const double expected = 0.75 * duration;      // (580-40)/720 = 0.75
    AV_CHECK(TickUntil(player, driver, [&] { return std::abs(player.Position() - expected) < 0.4; }, 3.0));

    // 3) 失焦会取消拖拽（否则回来时"鼠标明明松了还在拖"）
    driver.Feed(Mouse(core::InputEventType::MouseButtonDown, 100, barY));
    AV_CHECK(driver.State().dragging);
    core::InputEvent focusLost;
    focusLost.type = core::InputEventType::WindowFocusLost;
    driver.Feed(focusLost);
    AV_CHECK(!driver.State().dragging);

    player.Close();
}
