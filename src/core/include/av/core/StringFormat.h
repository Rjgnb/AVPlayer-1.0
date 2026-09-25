#pragma once

#include <string>

namespace av::core {

// 00:01:23 / 1:23.4 —— UI 展示用
std::string FormatTimecode(double seconds);
// "1.50x"
std::string FormatSpeed(double speed);
// 十六进制 + 可读文本
std::string FormatHex(std::uint64_t value);

} // namespace av::core