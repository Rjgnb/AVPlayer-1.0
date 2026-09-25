#include "av/media/Decoder.h"

#include "av/core/Log.h"
#include "av/media/FFmpegUtil.h"

namespace av::media {

namespace {
constexpr const char* kTag = "decode";
}

Decoder::~Decoder()
{
    Close();
}

Decoder::Decoder(Decoder&& other) noexcept
    : codec_(other.codec_), mediaType_(other.mediaType_), draining_(other.draining_), drained_(other.drained_)
{
    other.codec_    = nullptr;
    other.draining_ = false;
    other.drained_  = false;
}

Decoder& Decoder::operator=(Decoder&& other) noexcept
{
    if (this != &other)
    {
        Close();
        codec_     = other.codec_;
        mediaType_ = other.mediaType_;
        draining_  = other.draining_;
        drained_   = other.drained_;
        other.codec_    = nullptr;
        other.draining_ = false;
        other.drained_  = false;
    }
    return *this;
}

core::Status Decoder::Open(AVStream* stream, int threads)
{
    Close();
    if (stream == nullptr)
    {
        return core::Status::Error(core::StatusCode::InvalidArgument, "stream 为空");
    }

    const AVCodecID codecId = stream->codecpar->codec_id;
    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (codec == nullptr)
    {
        return core::Status::Error(core::StatusCode::Unsupported,
                                   std::string("没有可用的解码器: ") + avcodec_get_name(codecId));
    }

    codec_ = avcodec_alloc_context3(codec);
    if (codec_ == nullptr)
    {
        return core::Status::Error(core::StatusCode::Internal, "avcodec_alloc_context3 失败");
    }

    int result = avcodec_parameters_to_context(codec_, stream->codecpar);
    if (result < 0)
    {
        const core::Status status = FromAvError(result, "avcodec_parameters_to_context");
        Close();
        return status;
    }

    // 帧的时间基：pkt_timebase 是"交给 avcodec_receive_frame 的帧"所用的时基
    codec_->pkt_timebase = stream->time_base;

    if (codec_->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        // 0 = 让 FFmpeg 按 CPU 核数自己决定；调试时可强制单线程
        codec_->thread_count = threads;
        if (threads != 1) codec_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    result = avcodec_open2(codec_, codec, nullptr);
    if (result < 0)
    {
        const core::Status status = FromAvError(result, std::string("avcodec_open2(") + codec->name + ")");
        Close();
        return status;
    }

    mediaType_ = codec_->codec_type;
    AV_LOG(core::LogLevel::Debug, kTag)
        << "解码器就绪: " << (codec->long_name != nullptr ? codec->long_name : codec->name)
        << " 线程=" << codec_->thread_count;
    return core::Status::Ok();
}

void Decoder::Close()
{
    if (codec_ != nullptr)
    {
        avcodec_free_context(&codec_);
    }
    mediaType_ = AVMEDIA_TYPE_UNKNOWN;
    draining_  = false;
    drained_   = false;
}

std::string Decoder::CodecName() const
{
    if (codec_ == nullptr || codec_->codec == nullptr) return {};
    return codec_->codec->name != nullptr ? codec_->codec->name : std::string();
}

core::Status Decoder::Decode(const Packet* packet, std::vector<Frame>& out)
{
    if (codec_ == nullptr)
    {
        return core::Status::Error(core::StatusCode::Internal, "解码器未打开");
    }
    if (drained_) return core::Status::Ok();
    if (packet == nullptr) draining_ = true;

    const int sendResult = avcodec_send_packet(codec_, packet != nullptr ? packet->Raw() : nullptr);
    if (sendResult == AVERROR_EOF)
    {
        drained_ = true;
        return core::Status::Ok();
    }
    if (sendResult < 0 && sendResult != AVERROR(EAGAIN))
    {
        return FromAvError(sendResult, "avcodec_send_packet");
    }

    for (;;)
    {
        Frame frame(FromAVRational(codec_->pkt_timebase));
        if (!frame.IsAllocated())
        {
            return core::Status::Error(core::StatusCode::Internal, "av_frame_alloc 失败");
        }

        const int receiveResult = avcodec_receive_frame(codec_, frame.Raw());
        if (receiveResult == AVERROR(EAGAIN)) break;              // 需要更多输入
        if (receiveResult == AVERROR_EOF)
        {
            drained_ = true;
            break;
        }
        if (receiveResult < 0)
        {
            return FromAvError(receiveResult, "avcodec_receive_frame");
        }

        out.push_back(std::move(frame));
    }

    return core::Status::Ok();
}

void Decoder::Flush()
{
    if (codec_ != nullptr)
    {
        avcodec_flush_buffers(codec_);
    }
    draining_ = false;
    drained_  = false;
}

} // namespace av::media