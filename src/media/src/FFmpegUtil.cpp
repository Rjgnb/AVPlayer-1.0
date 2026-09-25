#include "av/media/FFmpegUtil.h"

#include <array>
#include <cerrno>

namespace av::media {

std::string AvErrorString(int errorCode)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    if (av_strerror(errorCode, buffer.data(), buffer.size()) < 0)
    {
        return "未知错误 " + std::to_string(errorCode);
    }
    return buffer.data();
}

core::Status FromAvError(int errorCode, std::string_view what)
{
    std::string message(what);
    message += " 失败: ";
    message += AvErrorString(errorCode);

    core::StatusCode code = core::StatusCode::Internal;
    if (errorCode == AVERROR(EAGAIN))          code = core::StatusCode::Timeout;
    else if (errorCode == AVERROR_EOF)         code = core::StatusCode::Io;
    else if (errorCode == AVERROR_EXIT)        code = core::StatusCode::Cancelled;
    else if (errorCode == AVERROR(EINVAL))     code = core::StatusCode::InvalidArgument;
    else if (errorCode == AVERROR(ENOMEM))     code = core::StatusCode::Internal;
    else if (errorCode == AVERROR(ENOENT))     code = core::StatusCode::NotFound;
    else if (errorCode == AVERROR(ENOSYS))     code = core::StatusCode::Unsupported;
    else if (errorCode == AVERROR(EPERM))      code = core::StatusCode::Io;
    else if (errorCode == AVERROR_INVALIDDATA) code = core::StatusCode::Decode;

    return core::Status::Error(code, std::move(message), errorCode);
}

core::PixelFormat FromAVPixelFormat(int format) noexcept
{
    switch (format)
    {
    case AV_PIX_FMT_YUV420P: return core::PixelFormat::Yuv420P;
    case AV_PIX_FMT_NV12:    return core::PixelFormat::Nv12;
    case AV_PIX_FMT_BGRA:    return core::PixelFormat::Bgra;
    case AV_PIX_FMT_RGBA:    return core::PixelFormat::Rgba;
    default:                 return core::PixelFormat::Unknown;
    }
}

core::SampleFormat FromAVSampleFormat(int format) noexcept
{
    switch (format)
    {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:  return core::SampleFormat::U8;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P: return core::SampleFormat::S16;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P: return core::SampleFormat::S32;
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP: return core::SampleFormat::F32;
    default:                 return core::SampleFormat::Unknown;
    }
}

int ToAVPixelFormat(core::PixelFormat format) noexcept
{
    switch (format)
    {
    case core::PixelFormat::Yuv420P: return AV_PIX_FMT_YUV420P;
    case core::PixelFormat::Nv12:    return AV_PIX_FMT_NV12;
    case core::PixelFormat::Bgra:    return AV_PIX_FMT_BGRA;
    case core::PixelFormat::Rgba:    return AV_PIX_FMT_RGBA;
    case core::PixelFormat::Unknown: break;
    }
    return AV_PIX_FMT_NONE;
}

int ToAVSampleFormat(core::SampleFormat format) noexcept
{
    switch (format)
    {
    case core::SampleFormat::U8:  return AV_SAMPLE_FMT_U8;
    case core::SampleFormat::S16: return AV_SAMPLE_FMT_S16;
    case core::SampleFormat::S32: return AV_SAMPLE_FMT_S32;
    case core::SampleFormat::F32: return AV_SAMPLE_FMT_FLT;
    case core::SampleFormat::Unknown: break;
    }
    return AV_SAMPLE_FMT_NONE;
}

bool IsPlanarSampleFormat(int avSampleFormat) noexcept
{
    return av_sample_fmt_is_planar(static_cast<AVSampleFormat>(avSampleFormat)) == 1;
}

} // namespace av::media
