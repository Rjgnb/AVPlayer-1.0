#include "av/media/Frame.h"

#include "av/media/FFmpegUtil.h"

namespace av::media {

Frame::Frame(core::Rational timeBase) : timeBase_(timeBase)
{
    frame_ = av_frame_alloc();
}

Frame::~Frame()
{
    av_frame_free(&frame_);
}

Frame::Frame(Frame&& other) noexcept : frame_(other.frame_), timeBase_(other.timeBase_)
{
    other.frame_ = nullptr;
}

Frame& Frame::operator=(Frame&& other) noexcept
{
    if (this != &other)
    {
        av_frame_free(&frame_);
        frame_       = other.frame_;
        timeBase_    = other.timeBase_;
        other.frame_ = nullptr;
    }
    return *this;
}

double Frame::PtsSeconds() const noexcept
{
    if (!frame_) return 0.0;
    std::int64_t timestamp = frame_->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE) timestamp = frame_->pts;
    if (timestamp == AV_NOPTS_VALUE) return 0.0;
    return core::ToSeconds(timestamp, timeBase_);
}

double Frame::DurationSeconds() const noexcept
{
    if (!frame_) return 0.0;
    if (frame_->duration > 0) return core::ToSeconds(frame_->duration, timeBase_);
    // 音频：用采样数换算（很多解码器不填 duration）
    if (frame_->sample_rate > 0 && frame_->nb_samples > 0)
    {
        return static_cast<double>(frame_->nb_samples) / frame_->sample_rate;
    }
    return 0.0;
}

core::VideoFrameView MakeVideoView(const Frame& frame, const core::VideoFormat& format, double durationSeconds)
{
    core::VideoFrameView view;
    const AVFrame* raw = frame.Raw();
    if (raw == nullptr) return view;

    const int planeCount = format.format == core::PixelFormat::Yuv420P ? 3
                         : format.format == core::PixelFormat::Nv12    ? 2
                                                                       : 1;
    for (int i = 0; i < planeCount && i < 4; ++i)
    {
        view.planes[i]  = raw->data[i];
        view.strides[i] = raw->linesize[i];
    }
    view.format          = format;
    view.ptsSeconds      = frame.PtsSeconds();
    view.durationSeconds = durationSeconds > 0.0 ? durationSeconds : frame.DurationSeconds();
    return view;
}

core::VideoFormat VideoFormatOf(const Frame& frame, core::PixelFormat* outPixelFormat)
{
    core::VideoFormat format;
    const AVFrame* raw = frame.Raw();
    if (raw == nullptr) return format;

    format.width  = raw->width;
    format.height = raw->height;
    format.format = FromAVPixelFormat(raw->format);
    format.frameRate = core::Rational{ 0, 1 };
    if (outPixelFormat != nullptr) *outPixelFormat = format.format;
    return format;
}

core::AudioFormat AudioFormatOf(const Frame& frame)
{
    core::AudioFormat format;
    const AVFrame* raw = frame.Raw();
    if (raw == nullptr) return format;

    format.sampleRate = raw->sample_rate;
    format.channels   = raw->ch_layout.nb_channels;
    format.format     = FromAVSampleFormat(raw->format);
    return format;
}

core::AudioFrameView MakeAudioView(const Frame& frame, const core::AudioFormat& format)
{
    core::AudioFrameView view;
    const AVFrame* raw = frame.Raw();
    if (raw == nullptr) return view;

    const bool planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(raw->format)) == 1;
    if (planar)
    {
        for (int i = 0; i < format.channels && i < 8; ++i) view.data[i] = raw->extended_data[i];
    }
    else
    {
        view.data[0] = raw->extended_data[0];
    }
    view.nbSamples  = raw->nb_samples;
    view.format     = format;
    view.ptsSeconds = frame.PtsSeconds();
    return view;
}

} // namespace av::media
