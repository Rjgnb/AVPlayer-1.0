// media 层单测：直接验证"解封装 + 解码"这条最容易出错的路。
//
// 为什么必须有这一层测试？A/V 同步的全部前提是"帧上的 pts 是对的"。
// 一旦时基算错，pts 会全变成 0，症状是"疯狂丢帧 + 播不到结尾"——
// 这类 bug 从上层（Player）很难看出根因，在 media 层断言 pts 一眼就能定位。
#include "TestHarness.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "av/media/Decoder.h"
#include "av/media/Demuxer.h"
#include "av/media/Frame.h"
#include "av/media/AudioResampler.h"
#include "av/media/FFmpegCompat.h"
#include "av/media/MediaInfo.h"
#include "av/media/Packet.h"

using namespace av;

namespace {

std::string SamplePath()
{
#if defined(AVPLAYER_TEST_MEDIA_DIR)
    const std::filesystem::path dir{ AVPLAYER_TEST_MEDIA_DIR };
    const std::filesystem::path file = dir / "1-zzitai-480P-AVC 00_00_00-00_00_05.mp4";
    if (std::filesystem::exists(file)) return file.string();
#endif
    return {};
}

void SkipNotice()
{
    std::cout << "         (跳过：找不到样例视频，设置 -DAVPLAYER_TEST_MEDIA_DIR=<目录>)\n";
}

} // namespace

AV_TEST(Media_视频解码_pts从0开始且单调递增)
{
    const std::string sample = SamplePath();
    if (sample.empty()) { SkipNotice(); return; }

    media::Demuxer     demuxer;
    media::DemuxOptions options;
    options.enableAudio = false;
    AV_CHECK(demuxer.Open(sample, options).ok());

    const media::MediaInfo& info = demuxer.Info();
    AV_CHECK(info.hasVideo);
    AV_CHECK(info.video.timeBase.num != 0);           // 时基必须有值：为 0 的话 pts 会全是 0
    AV_CHECK(info.video.stream != nullptr);

    media::Decoder decoder;
    AV_CHECK(decoder.Open(info.video.stream, 0).ok());   // 0 = 让 FFmpeg 自己决定线程数

    std::vector<double>       pts;
    std::vector<media::Frame> frames;
    for (int guard = 0; pts.size() < 30 && guard < 500; ++guard)
    {
        media::Packet packet;
        const media::Demuxer::ReadResult result = demuxer.Read(packet);
        if (result == media::Demuxer::ReadResult::EndOfStream) break;
        AV_CHECK(result == media::Demuxer::ReadResult::Ok);

        frames.clear();
        AV_CHECK(decoder.Decode(&packet, frames).ok());
        for (media::Frame& frame : frames)
        {
            if (frame.HasData()) pts.push_back(frame.PtsSeconds());
        }
    }

    AV_CHECK(pts.size() >= 30);
    AV_CHECK_NEAR(pts.front(), 0.0, 0.2);             // 第一帧在 0 附近
    for (std::size_t i = 1; i < pts.size(); ++i)
    {
        AV_CHECK(pts[i] > pts[i - 1]);                // 严格递增
    }
    AV_CHECK_NEAR(pts[1] - pts[0], 1.0 / 30.0, 0.01); // 样例是 30fps
}

AV_TEST(Media_视频总帧数与时长一致)
{
    const std::string sample = SamplePath();
    if (sample.empty()) { SkipNotice(); return; }

    media::Demuxer     demuxer;
    media::DemuxOptions options;
    options.enableAudio = false;
    AV_CHECK(demuxer.Open(sample, options).ok());

    const media::MediaInfo& info = demuxer.Info();
    AV_CHECK(info.hasVideo);

    media::Decoder decoder;
    AV_CHECK(decoder.Open(info.video.stream, 1).ok());

    std::size_t               count = 0;
    std::vector<media::Frame> frames;
    for (;;)
    {
        media::Packet packet;
        const media::Demuxer::ReadResult result = demuxer.Read(packet);
        if (result != media::Demuxer::ReadResult::Ok) break;

        frames.clear();
        AV_CHECK(decoder.Decode(&packet, frames).ok());
        for (media::Frame& frame : frames)
        {
            if (frame.HasData()) ++count;
        }
    }

    // 解码器把 rest 吐完
    frames.clear();
    AV_CHECK(decoder.Decode(nullptr, frames).ok());
    for (media::Frame& frame : frames)
    {
        if (frame.HasData()) ++count;
    }

    const double frameRate = info.video.frameRate > 0.0 ? info.video.frameRate : 30.0;
    const double expected  = info.durationSeconds * frameRate;
    AV_CHECK_NEAR(static_cast<double>(count), expected, expected * 0.15);
}

AV_TEST(Media_音频解码_格式与pts正确)
{
    const std::string sample = SamplePath();
    if (sample.empty()) { SkipNotice(); return; }

    media::Demuxer     demuxer;
    media::DemuxOptions options;
    options.enableVideo = false;
    AV_CHECK(demuxer.Open(sample, options).ok());

    const media::MediaInfo& info = demuxer.Info();
    AV_CHECK(info.hasAudio);
    AV_CHECK_EQ(info.audio.format.sampleRate, 48000);
    AV_CHECK_EQ(info.audio.format.channels, 2);
    AV_CHECK(info.audio.timeBase.num != 0);

    media::Decoder decoder;
    AV_CHECK(decoder.Open(info.audio.stream, 1).ok());

    std::vector<double>       pts;
    std::vector<media::Frame> frames;
    for (int guard = 0; pts.size() < 10 && guard < 200; ++guard)
    {
        media::Packet packet;
        const media::Demuxer::ReadResult result = demuxer.Read(packet);
        if (result == media::Demuxer::ReadResult::EndOfStream) break;
        AV_CHECK(result == media::Demuxer::ReadResult::Ok);

        frames.clear();
        AV_CHECK(decoder.Decode(&packet, frames).ok());
        for (media::Frame& frame : frames)
        {
            if (frame.HasData()) pts.push_back(frame.PtsSeconds());
        }
    }

    AV_CHECK(pts.size() >= 10);
    for (std::size_t i = 1; i < pts.size(); ++i)
    {
        AV_CHECK(pts[i] > pts[i - 1]);
    }
    // 每帧 1024 采样 / 48000Hz
    AV_CHECK_NEAR(pts[1] - pts[0], 1024.0 / 48000.0, 0.005);
}

AV_TEST(Media_音频重采样_按真实平面布局读取FLTP)
{
    media::Frame frame(core::Rational{ 1, 48000 });
    AVFrame* raw = frame.Raw();
    AV_CHECK(raw != nullptr);

    raw->format        = AV_SAMPLE_FMT_FLTP;
    raw->sample_rate   = 48000;
    raw->nb_samples    = 1024;
    raw->pts           = 0;
    av_channel_layout_default(&raw->ch_layout, 2);
    AV_CHECK(av_frame_get_buffer(raw, 0) >= 0);

    // 两声道平面各填不同常量：左=+1，右=-1。若把 FLTP 错当成 packed FLT，
    // 输出两声道会近似相同；按真实平面布局读取时，输出必须保持 +1/-1 对。
    for (int channel = 0; channel < 2; ++channel)
    {
        auto* samples = reinterpret_cast<float*>(raw->extended_data[channel]);
        for (int i = 0; i < raw->nb_samples; ++i)
        {
            samples[i] = channel == 0 ? 1.0f : -1.0f;
        }
    }

    const core::AudioFormat format{ 48000, 2, core::SampleFormat::F32 };
    media::AudioResampler resampler;
    AV_CHECK(resampler.Configure(format, format, 1.0).ok());

    const core::Result<core::AudioFrameView> converted = resampler.Convert(frame, frame.PtsSeconds());
    AV_CHECK(converted.ok());
    AV_CHECK(converted.value().nbSamples > 0);
    AV_CHECK(converted.value().data[0] != nullptr);

    const auto* output = reinterpret_cast<const float*>(converted.value().data[0]);
    AV_CHECK_NEAR(output[0], 1.0, 0.05);
    AV_CHECK_NEAR(output[1], -1.0, 0.05);
}
