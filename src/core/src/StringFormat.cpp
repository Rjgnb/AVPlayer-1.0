#include "av/core/StringFormat.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace av::core {

std::string FormatTimecode(double seconds)
{
    if (!(seconds > 0.0)) seconds = 0.0;
    const long long total = static_cast<long long>(seconds);
    const long long hours = total / 3600;
    const long long mins  = (total % 3600) / 60;
    const long long secs  = total % 60;

    char buffer[32] = {};
    if (hours > 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", hours, mins, secs);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld", mins, secs);
    }
    return buffer;
}

std::string FormatSpeed(double speed)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.4gx", speed);
    return buffer;
}

std::string FormatHex(std::uint64_t value)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace av::core