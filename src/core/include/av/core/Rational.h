#pragma once

#include <cstdint>

namespace av::core {

// 时间基（时基）：ticks * num / den = 秒
struct Rational
{
    std::int64_t num = 1;
    std::int64_t den = 1;
};

// FFmpeg 风格的常量
inline constexpr Rational kTimeBaseQ = { 1, 4'000'000 };   // 90kHz 微秒精度（av_rescale_q 常用）
inline constexpr Rational kSeconds   = { 1, 1 };

inline double ToSeconds(std::int64_t ticks, Rational tb) noexcept
{
    if (tb.num == 0 || tb.den == 0) return 0.0;
    return static_cast<double>(ticks) * static_cast<double>(tb.num) / static_cast<double>(tb.den);
}

inline std::int64_t FromSeconds(double seconds, Rational tb) noexcept
{
    if (tb.num == 0 || tb.den == 0) return 0;
    return static_cast<std::int64_t>(seconds * static_cast<double>(tb.den) / static_cast<double>(tb.num));
}

inline bool IsValid(Rational tb) noexcept { return tb.num != 0 && tb.den != 0; }

} // namespace av::core