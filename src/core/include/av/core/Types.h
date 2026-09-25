#pragma once

#include <cstdint>
#include <string>

#include "av/core/Rational.h"

namespace av::core {

// ---------------------------------------------------------------------------
// 几何 / 颜色
// ---------------------------------------------------------------------------
struct Size
{
    int width = 0;
    int height = 0;
};

inline bool operator==(const Size& lhs, const Size& rhs) noexcept
{
    return lhs.width == rhs.width && lhs.height == rhs.height;
}
inline bool operator!=(const Size& lhs, const Size& rhs) noexcept { return !(lhs == rhs); }

struct Point
{
    int x = 0;
    int y = 0;
};

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct Line
{
    Point a;
    Point b;
};

struct Color
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

inline constexpr Color kBlack      = { 0, 0, 0, 255 };
inline constexpr Color kWhite      = { 255, 255, 255, 255 };
inline constexpr Color kTransparent = { 0, 0, 0, 0 };

inline bool Contains(const Rect& r, Point p) noexcept
{
    return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height;
}

// ---------------------------------------------------------------------------
// 像素 / 采样格式
//   只列本项目用到的格式：未知 -> 让后端报"不支持"，而不是崩溃
// ---------------------------------------------------------------------------
enum class PixelFormat
{
    Unknown,
    Bgra,
    Rgba,
    Yuv420P,
    Nv12,
};

enum class SampleFormat
{
    Unknown,
    U8,
    S16,
    S32,
    F32,
};

// 每个采样点占多少字节
int BytesPerSample(SampleFormat format) noexcept;
const char* ToString(PixelFormat format) noexcept;
const char* ToString(SampleFormat format) noexcept;

struct AudioFormat
{
    int          sampleRate = 0;      // Hz
    int          channels   = 0;      // 交错/平面通道数
    SampleFormat format     = SampleFormat::Unknown;

    bool IsValid() const noexcept { return sampleRate > 0 && channels > 0 && format != SampleFormat::Unknown; }
    // 每秒"媒体时间"对应的字节数：时钟用它把"设备排队字节"换算成秒
    double ByteRate() const noexcept
    {
        return static_cast<double>(sampleRate) * channels * BytesPerSample(format);
    }
};

inline bool operator==(const AudioFormat& lhs, const AudioFormat& rhs) noexcept
{
    return lhs.sampleRate == rhs.sampleRate && lhs.channels == rhs.channels && lhs.format == rhs.format;
}
inline bool operator!=(const AudioFormat& lhs, const AudioFormat& rhs) noexcept { return !(lhs == rhs); }

struct VideoFormat
{
    int        width  = 0;
    int        height = 0;
    PixelFormat format = PixelFormat::Unknown;
    Rational   frameRate{ 0, 1 };

    bool IsValid() const noexcept { return width > 0 && height > 0 && format != PixelFormat::Unknown; }
};

inline bool operator==(const VideoFormat& lhs, const VideoFormat& rhs) noexcept
{
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.format == rhs.format;
}
inline bool operator!=(const VideoFormat& lhs, const VideoFormat& rhs) noexcept { return !(lhs == rhs); }

// ---------------------------------------------------------------------------
// 帧视图：只读、不拥有内存
//
// 【所有权约定】Write()/Present() 返回之后，视图立即失效。
// 后端必须在返回前完成消费（拷贝到设备 / 上传纹理 / 自行持有副本）。
// 这样 media 层就能安全地复用同一块 AVFrame，避免每帧分配。
// ---------------------------------------------------------------------------
struct VideoFrameView
{
    const std::uint8_t* planes[4]  = { nullptr, nullptr, nullptr, nullptr };
    int                 strides[4] = { 0, 0, 0, 0 };
    VideoFormat         format;
    double              ptsSeconds = 0.0;
    double              durationSeconds = 0.0;
};

struct AudioFrameView
{
    const std::uint8_t* data[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    int                 nbSamples = 0;      // 每通道采样点数（不是字节数）
    AudioFormat         format;
    double              ptsSeconds = 0.0;
};

} // namespace av::core
