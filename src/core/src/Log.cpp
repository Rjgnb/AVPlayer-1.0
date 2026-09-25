#include "av/core/Log.h"

#include <cstdio>
#include <ctime>
#include <mutex>

namespace av::core {

const char* ToString(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Off:   return "OFF";
    }
    return "?";
}

bool ParseLogLevel(std::string_view text, LogLevel& out) noexcept
{
    struct Entry { std::string_view name; LogLevel level; };
    static constexpr Entry kEntries[] = {
        { "trace", LogLevel::Trace }, { "debug", LogLevel::Debug }, { "info", LogLevel::Info },
        { "warn",  LogLevel::Warn },  { "warning", LogLevel::Warn }, { "error", LogLevel::Error },
        { "off",   LogLevel::Off },   { "none", LogLevel::Off },
    };
    for (const auto& entry : kEntries)
    {
        if (entry.name == text) { out = entry.level; return true; }
    }
    return false;
}

void StderrLogSink::Write(const LogRecord& record)
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[16] = {};
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);

    std::fprintf(stderr, "%s %-5s [%.*s] %.*s\n",
                 stamp,
                 ToString(record.level),
                 static_cast<int>(record.category.size()), record.category.data(),
                 static_cast<int>(record.message.size()), record.message.data());
    std::fflush(stderr);
}

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

Logger::Logger() = default;

void Logger::SetSink(ILogSink* sink)
{
    sink_.store(sink);
}

void Logger::SetLevel(LogLevel level)
{
    level_.store(static_cast<int>(level));
}

LogLevel Logger::Level() const
{
    return static_cast<LogLevel>(level_.load());
}

bool Logger::IsEnabled(LogLevel level) const
{
    if (level == LogLevel::Off) return false;
    return static_cast<int>(level) >= level_.load();
}

void Logger::Write(LogLevel level, std::string_view category, std::string message)
{
    if (!IsEnabled(level)) return;

    ILogSink* sink = sink_.load();
    const LogRecord record{ level, category, message };

    // 加锁保证一行的完整性（多线程下日志交错会非常难读）
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (sink == nullptr) sink = &defaultSink_;
    sink->Write(record);
}

} // namespace av::core