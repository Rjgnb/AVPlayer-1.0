#pragma once

#include "av/media/FFmpegCompat.h"

// ---------------------------------------------------------------------------
// 【本文件属于 media 层】这一层是 FFmpeg 的"适配器层"：
// 上层（player）可以用这里暴露的类型，但"换掉 FFmpeg"只需要改这一层。
// 其它层（core / ui / output）不允许包含 FFmpeg 头文件。
// ---------------------------------------------------------------------------
#include <string>
#include <string_view>

#include "av/core/Rational.h"
#include "av/core/Status.h"
#include "av/core/Types.h"

namespace av::media {

inline core::Rational FromAVRational(AVRational value) noexcept
{
    return core::Rational{ value.num, value.den };
}

inline AVRational ToAVRational(core::Rational value) noexcept
{
    return AVRational{ static_cast<int>(value.num), static_cast<int>(value.den) };
}

// FFmpeg 错误码 -> 中性 Status（把"具体库的错误域"挡在 media 层内部）
core::Status FromAvError(int errorCode, std::string_view what);

// 人类可读的 FFmpeg 错误文本
std::string AvErrorString(int errorCode);

// 像素/采样格式映射；不支持时返回 core::*::Unknown
core::PixelFormat  FromAVPixelFormat(int format) noexcept;
core::SampleFormat FromAVSampleFormat(int format) noexcept;
int                ToAVPixelFormat(core::PixelFormat format) noexcept;
int                ToAVSampleFormat(core::SampleFormat format) noexcept;

// 判断某像素格式是否需要"平面->打包"的额外处理
bool IsPlanarSampleFormat(int avSampleFormat) noexcept;

} // namespace av::media
