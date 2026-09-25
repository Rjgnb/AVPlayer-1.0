#include "TestHarness.h"

#include "av/core/Clock.h"
#include "av/output/BackendRegistry.h"
#include "av/output/NullBackend.h"

using namespace av;

namespace {

core::AudioFrameView MakePcm(int samples, core::AudioFormat format)
{
    static std::vector<std::uint8_t> buffer;
    buffer.assign(static_cast<std::size_t>(samples) * format.channels * core::BytesPerSample(format.format), 0);

    core::AudioFrameView view;
    view.data[0]    = buffer.data();
    view.nbSamples  = samples;
    view.format     = format;
    view.ptsSeconds = 0.0;
    return view;
}

} // namespace

AV_TEST(NullAudioSink_按墙钟消耗字节)
{
    core::ManualClock      clock;
    av::output::NullAudioSink sink;
    sink.SetClock(&clock);

    const core::AudioFormat format{ 48000, 2, core::SampleFormat::S16 };
    AV_CHECK(sink.Open(format, av::output::AudioSinkConfig{}).ok());

    AV_CHECK(sink.Write(MakePcm(48000, format)).ok());       // 1 秒音频
    AV_CHECK_EQ(sink.QueuedBytes(), std::size_t(192000));

    clock.Advance(0.5);                                      // 过了半秒
    AV_CHECK_EQ(sink.QueuedBytes(), std::size_t(96000));

    clock.Advance(10.0);                                     // 早就播完了
    AV_CHECK_EQ(sink.QueuedBytes(), std::size_t(0));
}

AV_TEST(NullAudioSink_暂停时不再消耗)
{
    core::ManualClock      clock;
    av::output::NullAudioSink sink;
    sink.SetClock(&clock);

    const core::AudioFormat format{ 48000, 2, core::SampleFormat::S16 };
    sink.Open(format, av::output::AudioSinkConfig{});
    sink.Write(MakePcm(4800, format));

    sink.Pause(true);
    clock.Advance(5.0);
    AV_CHECK_EQ(sink.QueuedBytes(), std::size_t(4800 * 4));

    sink.Flush();
    AV_CHECK_EQ(sink.QueuedBytes(), std::size_t(0));
}

AV_TEST(NullVideoSink_记录帧与pts)
{
    av::output::NullVideoSink sink;
    const core::VideoFormat format{ 640, 360, core::PixelFormat::Yuv420P, core::Rational{ 25, 1 } };
    AV_CHECK(sink.Open(format, av::output::VideoSinkConfig{}).ok());

    core::VideoFrameView frame;
    frame.format     = format;
    frame.ptsSeconds = 6.5;
    AV_CHECK(sink.Draw(frame).ok());
    sink.Present();
    sink.Present();

    AV_CHECK_EQ(sink.FrameCount(), std::size_t(2));
    AV_CHECK_NEAR(sink.LastPtsSeconds(), 6.5, 1e-9);
    AV_CHECK(sink.RenderTarget() != nullptr);
}

// 契约：没有可绘制区域就必须返回 nullptr。
// 曾经这里无条件返回一个 1280x720 的"假"目标 —— 覆盖层于是被画到一块并不存在的画布上。
AV_TEST(NullVideoSink_未打开时没有渲染目标)
{
    av::output::NullVideoSink sink;
    AV_CHECK(sink.RenderTarget() == nullptr);
    AV_CHECK((sink.RenderSize() == core::Size{ 0, 0 }));

    const core::VideoFormat format{ 640, 360, core::PixelFormat::Yuv420P, core::Rational{ 25, 1 } };
    AV_CHECK(sink.Open(format, av::output::VideoSinkConfig{}).ok());
    AV_CHECK(sink.RenderTarget() != nullptr);
    AV_CHECK((sink.RenderSize() == core::Size{ 640, 360 }));   // 没指定窗口尺寸 -> 用视频尺寸

    av::output::VideoSinkConfig scaled;
    scaled.windowWidth  = 800;
    scaled.windowHeight = 600;
    AV_CHECK(sink.Open(format, scaled).ok());
    AV_CHECK((sink.RenderSize() == core::Size{ 800, 600 }));   // 指定了窗口尺寸 -> 用窗口尺寸

    sink.Close();
    AV_CHECK(sink.RenderTarget() == nullptr);
}

AV_TEST(NullEventSource_可脚本化)
{
    av::output::NullEventSource source;
    core::InputEvent            event;
    AV_CHECK(!source.Poll(event));

    core::InputEvent pushed;
    pushed.type = core::InputEventType::KeyDown;
    pushed.key  = core::KeyCode::Space;
    source.Push(pushed);

    AV_CHECK(source.Poll(event));
    AV_CHECK(event.key == core::KeyCode::Space);
    AV_CHECK(!source.Poll(event));
    AV_CHECK(!source.QuitRequested());
}

AV_TEST(BackendRegistry_注册查找默认值)
{
    av::output::BackendRegistry& registry = av::output::BackendRegistry::Instance();
    av::output::RegisterNullBackend();                      // 幂等：保证内建 null 后端可用
    registry.Register("test-null", [] { return std::make_unique<av::output::NullBackend>(); });

    AV_CHECK(registry.Contains("test-null"));
    AV_CHECK(registry.Contains("null"));                    // 内建后端已注册
    AV_CHECK(registry.Create("test-null") != nullptr);
    AV_CHECK(registry.Create("不存在") == nullptr);

    // 空名字 -> 默认后端
    const std::string defaultName = registry.DefaultName();
    AV_CHECK(!defaultName.empty());
    AV_CHECK(registry.Create().get() != nullptr);
}
