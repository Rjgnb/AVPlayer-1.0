#pragma once

#include <cstdint>

namespace av::core {

// 世代号：每次 seek/重开自增；消费端丢弃"旧世代"的数据，
// 从而避免 seek 之后旧帧串入新位置（老代码里靠标志位猜，容易漏）。
using Generation = std::uint64_t;

inline constexpr Generation kInvalidGeneration = 0;

class GenerationCounter
{
public:
    Generation Next() noexcept { return ++value_; }
    Generation Current() const noexcept { return value_; }
    void       Reset() noexcept { value_ = 0; }

private:
    Generation value_ = 0;
};

template <class T>
struct Tagged
{
    T          value{};
    Generation generation = kInvalidGeneration;
};

} // namespace av::core