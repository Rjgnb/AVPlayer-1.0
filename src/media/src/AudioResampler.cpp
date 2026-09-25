#include "av/media/AudioResampler.h"

#include "av/core/Log.h"
#include "av/media/FFmpegUtil.h"
#include "av/media/Frame.h"

#include <cmath>
#include <vector>

namespace av::media {

namespace {
constexpr const char* kTag = "resample";
constexpr double kMinSpeed = 0.05;
constexpr double kMaxSpeed = 16.0;

int RateFor(const core::AudioFormat& format, double speed)
{
    const double scaled = static_cast<double>(format.sampleRate) * speed;
    const long long rounded = std::llround(scaled);
    return rounded < 1 ? 1 : static_cast<int>(rounded);
}
} // namespace

struct AudioResampler::Impl
{
    SwrContext*               swr = nullptr;
    core::AudioFormat         input;
    core::AudioFormat         output;
    // core::SampleFormat 只表达“样本类型”，刻意不泄露 FFmpeg 的 packed/planar 细节。
    // 但解码帧的 AVSampleFormat 可能是 FLTP/S16P...，若这里仍按 packed 配置 swr，
    // swr_convert 会按错误的平面布局读取 extended_data（AAC 常见的就是 FLTP）。
    // 因此内部额外记住实际输入格式，并在首帧/格式变化时按帧的真实格式重建。
    int                       inputSampleFormat = AV_SAMPLE_FMT_NONE;
    double                    speed = 1.0;
    std::vector<std::uint8_t> buffer;
    std::vector<std::uint8_t*> planes{ nullptr };
    bool                      configured = false;

    ~Impl() { swr_free(&swr); }

    void Release()
    {
        swr_free(&swr);
        configured = false;
    }

    core::Status Rebuild()
    {
        Release();

        const int inputRate  = RateFor(input, speed);
        const int outputRate = output.sampleRate;

        AVChannelLayout inputLayout{};
        AVChannelLayout outputLayout{};
        av_channel_layout_default(&inputLayout, input.channels);
        av_channel_layout_default(&outputLayout, output.channels);

        // 关键：把输入采样率"假装"成 inputRate = 源采样率 * speed，
        // 于是 swr 会按 1/speed 的比例产出样本 —— 设备按原速率播放即"磁带式变速"。
        const int result = swr_alloc_set_opts2(&swr,
                                               &outputLayout,
                                               static_cast<AVSampleFormat>(ToAVSampleFormat(output.format)),
                                               outputRate,
                                               &inputLayout,
                                               static_cast<AVSampleFormat>(inputSampleFormat),
                                               inputRate,
                                               0,
                                               nullptr);
        av_channel_layout_uninit(&inputLayout);
        av_channel_layout_uninit(&outputLayout);

        if (result < 0 || swr == nullptr)
        {
            return FromAvError(result, "swr_alloc_set_opts2");
        }

        const int initResult = swr_init(swr);
        if (initResult < 0)
        {
            swr_free(&swr);
            return FromAvError(initResult, "swr_init");
        }

        configured = true;
        AV_LOG(core::LogLevel::Debug, kTag)
            << "重采样 " << input.sampleRate << "Hz/" << input.channels << "ch/"
            << core::ToString(input.format) << " -> " << outputRate << "Hz/" << output.channels << "ch/"
            << core::ToString(output.format) << " 速度=" << speed;
        return core::Status::Ok();
    }
};

AudioResampler::AudioResampler() : impl_(std::make_unique<Impl>()) {}
AudioResampler::~AudioResampler() = default;
AudioResampler::AudioResampler(AudioResampler&& other) noexcept : impl_(std::move(other.impl_)) {}
AudioResampler& AudioResampler::operator=(AudioResampler&& other) noexcept
{
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

core::Status AudioResampler::Configure(const core::AudioFormat& input, const core::AudioFormat& output, double speed)
{
    if (!input.IsValid() || !output.IsValid())
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "音频格式无效");
    }
    Impl& impl = *impl_;
    impl.input = input;
    impl.output = output;
    impl.inputSampleFormat = ToAVSampleFormat(input.format);
    if (impl.inputSampleFormat == AV_SAMPLE_FMT_NONE)
    {
        return core::Status::Error(core::StatusCode::Unsupported, "不支持的输入采样格式");
    }
    impl.speed = speed < kMinSpeed ? kMinSpeed : (speed > kMaxSpeed ? kMaxSpeed : speed);
    return impl.Rebuild();
}

core::Status AudioResampler::SetSpeed(double speed)
{
    Impl& impl = *impl_;
    const double clamped = speed < kMinSpeed ? kMinSpeed : (speed > kMaxSpeed ? kMaxSpeed : speed);
    if (std::abs(clamped - impl.speed) < 1e-6) return core::Status::Ok();
    impl.speed = clamped;
    if (!impl.configured) return core::Status::Ok();
    return impl.Rebuild();
}

void AudioResampler::Close()
{
    impl_->Release();
}

bool AudioResampler::IsConfigured() const noexcept { return impl_->configured; }
double AudioResampler::Speed() const noexcept { return impl_->speed; }
const core::AudioFormat& AudioResampler::OutputFormat() const noexcept { return impl_->output; }
const core::AudioFormat& AudioResampler::InputFormat() const noexcept { return impl_->input; }

core::Result<core::AudioFrameView> AudioResampler::Convert(const Frame& frame, double ptsSeconds)
{
    Impl& impl = *impl_;
    if (!impl.configured)
    {
        return core::Status::Error(core::StatusCode::Internal, "重采样器未配置");
    }

    const AVFrame* source = frame.Raw();
    if (source == nullptr || source->nb_samples <= 0)
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "空音频帧");
    }

    // 容器的 codecpar 只会告诉我们“F32/S16”，不能区分 FLT 与 FLTP。
    // 解码器真正交给我们的 AVFrame 才是权威；发现实际布局不同就重建 swr。
    if (source->format != AV_SAMPLE_FMT_NONE && source->format != impl.inputSampleFormat)
    {
        impl.inputSampleFormat = source->format;
        const core::Status rebuilt = impl.Rebuild();
        if (!rebuilt.ok()) return rebuilt;
    }

    const int inputRate = RateFor(impl.input, impl.speed);
    const int64_t delay = swr_get_delay(impl.swr, inputRate);
    int maxOut = static_cast<int>(av_rescale_rnd(delay + source->nb_samples,
                                                 impl.output.sampleRate,
                                                 inputRate,
                                                 AV_ROUND_UP));
    if (maxOut < 1) maxOut = 1;

    const std::size_t bytesPerFrame = static_cast<std::size_t>(impl.output.channels) *
                                      static_cast<std::size_t>(core::BytesPerSample(impl.output.format));
    const std::size_t needed = static_cast<std::size_t>(maxOut) * bytesPerFrame;
    if (impl.buffer.size() < needed) impl.buffer.resize(needed);
    impl.planes[0] = impl.buffer.data();

    const int converted = swr_convert(impl.swr,
                                      impl.planes.data(),
                                      maxOut,
                                      static_cast<const std::uint8_t* const*>(source->extended_data),
                                      source->nb_samples);
    if (converted < 0)
    {
        return FromAvError(converted, "swr_convert");
    }

    core::AudioFrameView view;
    view.data[0]    = impl.buffer.data();
    view.nbSamples  = converted;
    view.format     = impl.output;
    view.ptsSeconds = ptsSeconds;
    return view;
}

} // namespace av::media
