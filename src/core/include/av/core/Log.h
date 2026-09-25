#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace av::core {

enum class LogLevel
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Off,
};

const char* ToString(LogLevel level) noexcept;
bool        ParseLogLevel(std::string_view text, LogLevel& out) noexcept;

struct LogRecord
{
    LogLevel         level = LogLevel::Info;
    std::string_view category;
    std::string_view message;
};

// 日志落地点：控制台 / 文件 / Qt 的 qDebug —— 核心只依赖这个接口
class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogRecord& record) = 0;
};

// 默认 sink：带时间戳打到 stderr
class StderrLogSink final : public ILogSink
{
public:
    void Write(const LogRecord& record) override;
};

// 转发到 std::function（方便测试断言日志）
class CallbackLogSink final : public ILogSink
{
public:
    using Callback = std::function<void(const LogRecord&)>;
    explicit CallbackLogSink(Callback cb) : callback_(std::move(cb)) {}
    void Write(const LogRecord& record) override { if (callback_) callback_(record); }

private:
    Callback callback_;
};

// 全局日志门面。线程安全；SetSink 应在起线程之前调用。
class Logger
{
public:
    static Logger& Instance();

    void SetSink(ILogSink* sink);          // 传 nullptr 恢复默认 stderr
    void SetLevel(LogLevel level);
    LogLevel Level() const;
    bool IsEnabled(LogLevel level) const;

    void Write(LogLevel level, std::string_view category, std::string message);

private:
    Logger();

    std::atomic<int>  level_{ static_cast<int>(LogLevel::Info) };
    std::atomic<ILogSink*> sink_{ nullptr };
    StderrLogSink      defaultSink_;
    std::mutex         writeMutex_;
};

// 流式日志：AV_LOG(LogLevel::Info, "demux") << "打开 " << url;
class LogMessage
{
public:
    LogMessage(LogLevel level, std::string_view category) : level_(level), category_(category) {}
    ~LogMessage() { Logger::Instance().Write(level_, category_, stream_.str()); }

    template <class T>
    LogMessage& operator<<(const T& value)
    {
        stream_ << value;
        return *this;
    }

private:
    LogLevel           level_;
    std::string_view   category_;
    std::ostringstream stream_;
};

} // namespace av::core

#define AV_LOG(level, category)                                        \
    if (!::av::core::Logger::Instance().IsEnabled(level)) {            \
    } else                                                             \
        ::av::core::LogMessage(level, category)