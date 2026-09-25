#include "av/core/Types.h"

namespace av::core {

int BytesPerSample(SampleFormat format) noexcept
{
    switch (format)
    {
    case SampleFormat::U8:  return 1;
    case SampleFormat::S16: return 2;
    case SampleFormat::S32: return 4;
    case SampleFormat::F32: return 4;
    case SampleFormat::Unknown: break;
    }
    return 0;
}

const char* ToString(PixelFormat format) noexcept
{
    switch (format)
    {
    case PixelFormat::Bgra:    return "Bgra";
    case PixelFormat::Rgba:    return "Rgba";
    case PixelFormat::Yuv420P: return "Yuv420P";
    case PixelFormat::Nv12:    return "Nv12";
    case PixelFormat::Unknown: break;
    }
    return "Unknown";
}

const char* ToString(SampleFormat format) noexcept
{
    switch (format)
    {
    case SampleFormat::U8:  return "U8";
    case SampleFormat::S16: return "S16";
    case SampleFormat::S32: return "S32";
    case SampleFormat::F32: return "F32";
    case SampleFormat::Unknown: break;
    }
    return "Unknown";
}

} // namespace av::core