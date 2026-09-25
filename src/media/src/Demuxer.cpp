#include "av/media/Demuxer.h"

#include "av/core/Log.h"
#include "av/media/FFmpegUtil.h"

namespace av::media {

namespace {
constexpr const char* kTag = "demux";

std::string SanitizeName(const char* name)
{
    return name != nullptr ? std::string(name) : std::string();
}
} // namespace

Demuxer::Demuxer() = default;

Demuxer::~Demuxer()
{
    Close();
}

Demuxer::Demuxer(Demuxer&& other) noexcept
    : format_(other.format_), options_(std::move(other.options_)), info_(std::move(other.info_)),
      interrupt_(std::move(other.interrupt_))
{
    other.format_ = nullptr;
    if (format_ != nullptr) format_->interrupt_callback.opaque = this;
}

Demuxer& Demuxer::operator=(Demuxer&& other) noexcept
{
    if (this != &other)
    {
        Close();
        format_    = other.format_;
        options_   = std::move(other.options_);
        info_      = std::move(other.info_);
        interrupt_ = std::move(other.interrupt_);
        other.format_ = nullptr;
        if (format_ != nullptr) format_->interrupt_callback.opaque = this;
    }
    return *this;
}

int Demuxer::InterruptThunk(void* opaque)
{
    auto* self = static_cast<Demuxer*>(opaque);
    if (self == nullptr || !self->interrupt_) return 0;
    return self->interrupt_() ? 1 : 0;
}

void Demuxer::SetInterruptCallback(std::function<bool()> callback)
{
    interrupt_ = std::move(callback);
}

core::Status Demuxer::Open(const std::string& url, const DemuxOptions& options)
{
    Close();
    if (url.empty())
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "url 为空");
    }

    options_ = options;

    AVFormatContext* context = avformat_alloc_context();
    if (context == nullptr)
    {
        return core::Status::Error(core::StatusCode::Internal, "avformat_alloc_context 失败");
    }
    // 中断回调必须在 open 之前装好：网络流/坏文件会卡在 open 里
    context->interrupt_callback.callback = &Demuxer::InterruptThunk;
    context->interrupt_callback.opaque   = this;

    AVDictionary* dict = nullptr;
    if (!options_.formatHint.empty())
    {
        av_dict_set(&dict, "f", options_.formatHint.c_str(), 0);
    }
    if (options_.preferLowLatency)
    {
        av_dict_set(&dict, "fflags", "nobuffer", 0);
        av_dict_set(&dict, "flags", "low_delay", 0);
    }
    if (!options_.allowNetwork)
    {
        // 默认只允许本地协议，避免"一个本地播放器被当成网络客户端"的意外
        av_dict_set(&dict, "protocol_whitelist", "file,crypto,data", 0);
    }

    const int openResult = avformat_open_input(&context, url.c_str(), nullptr, &dict);
    av_dict_free(&dict);
    if (openResult < 0)
    {
        avformat_close_input(&context);
        return FromAvError(openResult, "avformat_open_input(" + url + ")");
    }
    format_ = context;

    const int infoResult = avformat_find_stream_info(format_, nullptr);
    if (infoResult < 0)
    {
        const core::Status status = FromAvError(infoResult, "avformat_find_stream_info");
        Close();
        return status;
    }

    FillInfo();
    AV_LOG(core::LogLevel::Info, kTag)
        << "打开成功: " << info_.url << " [" << info_.containerFormat << "] "
        << "时长=" << info_.durationSeconds << "s 流数=" << info_.streamCount
        << " 编解码=" << info_.codecSummary;
    return core::Status::Ok();
}

void Demuxer::FillInfo()
{
    info_ = MediaInfo{};
    info_.url             = SanitizeName(format_->url);
    info_.containerFormat = SanitizeName(format_->iformat != nullptr ? format_->iformat->name : nullptr);
    info_.durationSeconds = format_->duration > 0 ? static_cast<double>(format_->duration) / AV_TIME_BASE : 0.0;
    info_.bitRate         = format_->bit_rate;
    info_.streamCount     = format_->nb_streams;

    std::string summary;

    if (options_.enableVideo)
    {
        const int index = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (index >= 0)
        {
            AVStream* stream = format_->streams[index];
            const AVCodecParameters* params = stream->codecpar;

            info_.hasVideo                  = true;
            info_.video.stream              = stream;
            info_.video.index               = index;
            info_.video.timeBase            = FromAVRational(stream->time_base);
            info_.video.bitRate             = params->bit_rate;
            info_.video.durationSeconds     = stream->duration > 0
                                                  ? core::ToSeconds(stream->duration, info_.video.timeBase)
                                                  : info_.durationSeconds;
            info_.video.format.width        = params->width;
            info_.video.format.height       = params->height;
            info_.video.format.format       = FromAVPixelFormat(params->format);
            info_.video.frameRate           = stream->avg_frame_rate.den != 0
                                                  ? av_q2d(stream->avg_frame_rate)
                                                  : 0.0;
            info_.video.format.frameRate    = FromAVRational(stream->avg_frame_rate);

            summary += avcodec_get_name(params->codec_id);
        }
    }

    if (options_.enableAudio)
    {
        const int index = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (index >= 0)
        {
            AVStream* stream = format_->streams[index];
            const AVCodecParameters* params = stream->codecpar;

            info_.hasAudio               = true;
            info_.audio.stream           = stream;
            info_.audio.index            = index;
            info_.audio.timeBase         = FromAVRational(stream->time_base);
            info_.audio.bitRate          = params->bit_rate;
            info_.audio.durationSeconds  = stream->duration > 0
                                               ? core::ToSeconds(stream->duration, info_.audio.timeBase)
                                               : info_.durationSeconds;
            info_.audio.format.sampleRate = params->sample_rate;
            info_.audio.format.channels   = params->ch_layout.nb_channels;
            info_.audio.format.format     = FromAVSampleFormat(params->format);

            if (!summary.empty()) summary += " + ";
            summary += avcodec_get_name(params->codec_id);
        }
    }

    info_.codecSummary = summary;
}

Demuxer::ReadResult Demuxer::Read(Packet& out, core::Status* error)
{
    if (format_ == nullptr)
    {
        if (error != nullptr)
        {
            *error = core::Status::Error(core::StatusCode::Internal, "Demuxer 未打开");
        }
        return ReadResult::Error;
    }

    // 防御：如果调用方传进来一个"已被 std::move 走"的 Packet（Raw() == nullptr），
    // av_read_frame 会直接空指针崩溃。这里明确报错 —— 误用变成错误信息，而不是崩溃。
    if (!out.IsAllocated())
    {
        if (error != nullptr)
        {
            *error = core::Status::Error(core::StatusCode::Internal,
                                         "Packet 未分配（是否已经被 std::move 走了？）");
        }
        return ReadResult::Error;
    }

    for (;;)
    {
        out.Reset();
        const int result = av_read_frame(format_, out.Raw());
        if (result == AVERROR_EOF)  return ReadResult::EndOfStream;
        if (result == AVERROR_EXIT) return ReadResult::Cancelled;
        if (result == AVERROR(EAGAIN)) continue;
        if (result < 0)
        {
            if (error != nullptr) *error = FromAvError(result, "av_read_frame");
            return ReadResult::Error;
        }

        const int index = out.StreamIndex();
        if (IsAudioStream(index) || IsVideoStream(index)) return ReadResult::Ok;

        // 字幕/封面等无关流：丢掉继续读（老代码会把它们当错误处理）
        out.Reset();
    }
}

core::Status Demuxer::SeekTo(double seconds)
{
    if (format_ == nullptr)
    {
        return core::Status::Error(core::StatusCode::Internal, "Demuxer 未打开");
    }
    if (!(seconds >= 0.0)) seconds = 0.0;

    // 用 AV_TIME_BASE + stream_index = -1，让 FFmpeg 自己挑时间基准。
    //
    // 一条重要的事实：seek 只能定位到"关键帧"。本工程的样例视频 154 个视频包里
    // 只有两个关键帧（0.0s 与 5.0s），所以跳到 3.85s 时 AVSEEK_FLAG_BACKWARD
    // 只能给到 0.0s 那个关键帧 —— 这不是 bug，是文件本身没有中间关键帧。
    // 各 pipeline 的 dropBefore 会把目标之前的帧丢掉，这才是"精确跳转"的实现方式。
    const std::int64_t timestamp = static_cast<std::int64_t>(seconds * AV_TIME_BASE);
    const int result = av_seek_frame(format_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
    if (result < 0)
    {
        return FromAvError(result, "av_seek_frame(" + std::to_string(seconds) + "s)");
    }

    // 不要在这里调用 avformat_flush()。它语义是"丢弃字节流解析器的缓冲"，
    // 不是 seek；av_seek_frame 已经负责重置 demuxer 的包队列和读位置。
    // 额外 flush 容易把 MP4/TS 的解析状态和首个关键帧一起丢掉，表现为
    // “跳转后只能读到一小段，随后提前 EOF”。解码器缓冲由各自的
    // pipeline 在新世代里调用 Decoder::Flush() 处理。

    AV_LOG(core::LogLevel::Info, kTag) << "seek -> " << seconds << "s";
    return core::Status::Ok();
}

void Demuxer::Close()
{
    if (format_ != nullptr)
    {
        avformat_close_input(&format_);
    }
    info_ = MediaInfo{};
}

} // namespace av::media
