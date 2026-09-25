#include "av/core/Status.h"

namespace av::core {

const char* ToString(StatusCode code) noexcept
{
    switch (code)
    {
    case StatusCode::Ok:              return "Ok";
    case StatusCode::InvalidArgument: return "InvalidArgument";
    case StatusCode::NotFound:        return "NotFound";
    case StatusCode::Unsupported:     return "Unsupported";
    case StatusCode::Io:              return "Io";
    case StatusCode::Decode:          return "Decode";
    case StatusCode::Backend:         return "Backend";
    case StatusCode::Timeout:         return "Timeout";
    case StatusCode::Cancelled:       return "Cancelled";
    case StatusCode::Internal:        return "Internal";
    }
    return "Unknown";
}

std::string Status::ToString() const
{
    if (ok()) return "Ok";
    std::string text = "[";
    text += ::av::core::ToString(code_);
    text += "] ";
    text += message_;
    if (systemCode_ != 0)
    {
        text += " (system=";
        text += std::to_string(systemCode_);
        text += ")";
    }
    return text;
}

} // namespace av::core