#include "TestHarness.h"

#include "av/core/Clock.h"
#include "av/core/MediaClock.h"

using namespace av;

AV_TEST(MediaClock_音频主时钟换算)
{
    core::ManualClock wall;
    core::MediaClock  clock(&wall);

    clock.Reset(0.0);
    clock.SetByteRate(48000.0 * 2 * 2);       // 48kHz 立体声 S16
    clock.NotifyAudioWritten(5.0);            // 刚写入设备的音频 pts

    // 设备里还有 1 秒的数据没播 -> 当前媒体时间 = 5 - 1 = 4
    AV_CHECK_NEAR(clock.AudioNow(48000 * 2 * 2), 4.0, 1e-6);
}

AV_TEST(MediaClock_倍速影响排队字节的换算)
{
    core::ManualClock wall;
    core::MediaClock  clock(&wall);

    clock.Reset(0.0);
    clock.SetByteRate(48000.0 * 2 * 2);
    clock.SetSpeed(2.0);
    clock.NotifyAudioWritten(5.0);

    // 2 倍速：1 秒的排队字节代表 2 秒媒体时间
    AV_CHECK_NEAR(clock.AudioNow(48000 * 2 * 2), 3.0, 1e-6);
    // 水位阈值也按倍速折算：0.3 秒媒体时间 -> 0.3 * byteRate / 2 字节
    AV_CHECK_NEAR(clock.WatermarkBytes(0.3), 0.3 * 192000.0 / 2.0, 1e-6);
}

AV_TEST(MediaClock_墙钟兜底与暂停)
{
    core::ManualClock wall;
    core::MediaClock  clock(&wall);

    clock.SetSource(core::MediaClock::Source::Wall);
    clock.Reset(10.0);

    wall.Advance(1.0);
    AV_CHECK_NEAR(clock.TickWall(), 11.0, 1e-9);

    clock.Pause(true);
    wall.Advance(5.0);
    AV_CHECK_NEAR(clock.TickWall(), 11.0, 1e-9);   // 暂停期间不动

    clock.Pause(false);
    wall.Advance(2.0);
    AV_CHECK_NEAR(clock.TickWall(), 13.0, 1e-9);
}

AV_TEST(MediaClock_倍速限幅)
{
    core::MediaClock clock;
    clock.SetSpeed(1000.0);
    AV_CHECK_NEAR(clock.Speed(), 16.0, 1e-9);
    clock.SetSpeed(0.0);
    AV_CHECK_NEAR(clock.Speed(), 0.05, 1e-9);
}

AV_TEST(MediaClock_倍速下墙钟推进更快)
{
    core::ManualClock wall;
    core::MediaClock  clock(&wall);

    clock.SetSource(core::MediaClock::Source::Wall);
    clock.SetSpeed(4.0);
    clock.Reset(0.0);

    wall.Advance(0.5);
    AV_CHECK_NEAR(clock.TickWall(), 2.0, 1e-9);
}