#include "TestHarness.h"

#include "av/ui/BitmapFont.h"
#include "av/ui/Overlay.h"

using namespace av;

namespace {

av::ui::OverlayState MakeState()
{
    av::ui::OverlayState state;
    state.duration = 100.0;
    state.position = 25.0;
    av::ui::OverlayController controller;
    controller.Layout(state, core::Size{ 800, 600 });
    return state;
}

core::InputEvent Key(core::KeyCode key)
{
    core::InputEvent event;
    event.type = core::InputEventType::KeyDown;
    event.key  = key;
    return event;
}

core::InputEvent Mouse(core::InputEventType type, int x, int y, core::MouseButton button = core::MouseButton::Left)
{
    core::InputEvent event;
    event.type     = type;
    event.button   = button;
    event.position = core::Point{ x, y };
    return event;
}

} // namespace

AV_TEST(Overlay_布局把进度条放在底部)
{
    av::ui::OverlayController controller;
    av::ui::OverlayState      state;
    controller.Layout(state, core::Size{ 800, 600 });

    AV_CHECK_EQ(state.bar.width, 800 - 80);
    AV_CHECK(state.bar.y > 500);
    AV_CHECK(state.barHit.height > state.bar.height);          // 命中区比轨道大，好抓
    AV_CHECK_EQ(state.hitArea.width, 800);
}

AV_TEST(Overlay_空格暂停_方向键跳转)
{
    av::ui::OverlayController controller;
    av::ui::OverlayState      state = MakeState();

    AV_CHECK(controller.Handle(Key(core::KeyCode::Space), state).type == av::ui::ActionType::TogglePause);

    const av::ui::Action forward = controller.Handle(Key(core::KeyCode::Right), state);
    AV_CHECK(forward.type == av::ui::ActionType::SeekRelative);
    AV_CHECK_NEAR(forward.value, 5.0, 1e-9);

    const av::ui::Action back = controller.Handle(Key(core::KeyCode::Left), state);
    AV_CHECK_NEAR(back.value, -5.0, 1e-9);

    const av::ui::Action quit = controller.Handle(Key(core::KeyCode::Escape), state);
    AV_CHECK(quit.type == av::ui::ActionType::Quit);
}

AV_TEST(Overlay_倍速按档位走)
{
    av::ui::OverlayController controller;
    av::ui::OverlayState      state = MakeState();
    state.speed = 1.0;

    const av::ui::Action faster = controller.Handle(Key(core::KeyCode::RightBracket), state);
    AV_CHECK(faster.type == av::ui::ActionType::SetSpeed);
    AV_CHECK_NEAR(faster.value, 1.25, 1e-9);

    state.speed = 2.0;
    const av::ui::Action slower = controller.Handle(Key(core::KeyCode::LeftBracket), state);
    AV_CHECK_NEAR(slower.value, 1.5, 1e-9);

    state.speed = 0.25;
    AV_CHECK_NEAR(controller.Handle(Key(core::KeyCode::LeftBracket), state).value, 0.25, 1e-9);  // 到顶不动
}

AV_TEST(Overlay_点击画面暂停_点击进度条不暂停)
{
    av::ui::OverlayController controller;
    av::ui::OverlayState      state = MakeState();

    const av::ui::Action onVideo = controller.Handle(Mouse(core::InputEventType::MouseButtonDown, 400, 200), state);
    AV_CHECK(onVideo.type == av::ui::ActionType::TogglePause);

    const av::ui::Action onBar = controller.Handle(Mouse(core::InputEventType::MouseButtonDown, 400, state.bar.y), state);
    AV_CHECK(onBar.IsNone());                       // 点在进度条上不算"点击暂停"
    AV_CHECK(state.dragging);
}

AV_TEST(Overlay_拖拽期间不发seek_松手才发)
{
    av::ui::OverlayController controller;
    av::ui::OverlayState      state = MakeState();

    controller.Handle(Mouse(core::InputEventType::MouseButtonDown, state.bar.x + 10, state.bar.y), state);
    AV_CHECK(state.dragging);

    // 拖到一半：只更新 UI
    const av::ui::Action during = controller.Handle(Mouse(core::InputEventType::MouseMove, state.bar.x + 400, state.bar.y), state);
    AV_CHECK(during.IsNone());
    AV_CHECK_NEAR(state.position, 100.0 * 400.0 / state.bar.width, 0.5);

    // 松手：一次性 seek 到目标
    const av::ui::Action release = controller.Handle(Mouse(core::InputEventType::MouseButtonUp, state.bar.x + 400, state.bar.y), state);
    AV_CHECK(release.type == av::ui::ActionType::SeekTo);
    AV_CHECK_NEAR(release.value, state.position, 1e-9);
    AV_CHECK(!state.dragging);
}

AV_TEST(Overlay_失焦取消拖拽)
{
    av::ui::OverlayController controller;
    av::ui::OverlayState      state = MakeState();

    controller.Handle(Mouse(core::InputEventType::MouseButtonDown, state.bar.x + 10, state.bar.y), state);
    AV_CHECK(state.dragging);

    core::InputEvent focus;
    focus.type = core::InputEventType::WindowFocusLost;
    controller.Handle(focus, state);
    AV_CHECK(!state.dragging);
}

AV_TEST(Overlay_静止自动隐藏_有输入又出现)
{
    av::ui::InputMapConfig map;
    map.autoHideSeconds = 2.5;
    av::ui::OverlayController controller(map);
    av::ui::OverlayState      state = MakeState();

    controller.Update(state, 1.0);
    AV_CHECK(state.visible);
    controller.Update(state, 2.0);
    AV_CHECK(!state.visible);

    controller.Handle(Key(core::KeyCode::Space), state);
    AV_CHECK(state.visible);
    AV_CHECK_NEAR(state.idleSeconds, 0.0, 1e-9);
}

AV_TEST(Overlay_渲染出的几何都在画面内)
{
    av::ui::OverlayState state = MakeState();
    state.playing = false;

    avtest::RecordingRenderTarget target(800, 600);
    av::ui::OverlayRenderer       renderer;
    renderer.Draw(state, target);

    AV_CHECK(!target.filled.empty());
    for (const core::Rect& rect : target.filled)
    {
        AV_CHECK(rect.width > 0 && rect.height > 0);
        AV_CHECK(rect.x >= -20 && rect.y >= -20);
        AV_CHECK(rect.x + rect.width <= 860);
        AV_CHECK(rect.y + rect.height <= 640);
    }
}

AV_TEST(Overlay_隐藏时不画任何东西)
{
    av::ui::OverlayState state = MakeState();
    state.visible = false;

    avtest::RecordingRenderTarget target(800, 600);
    av::ui::OverlayRenderer       renderer;
    renderer.Draw(state, target);
    AV_CHECK(target.filled.empty());
}