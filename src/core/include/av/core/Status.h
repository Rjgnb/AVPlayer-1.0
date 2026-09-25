#pragma once

#include <string>
#include <utility>

namespace av::core {

// 错误分类：足够粗，便于上层决定"重试 / 降级 / 弹窗 / 退出"
enum class StatusCode
{
    Ok = 0,
    InvalidArgument,   // 调用方传错参数（编程错误）
    NotFound,          // 文件/设备不存在
    Unsupported,       // 格式或能力不支持（后端常见）
    Io,                // 读写失败
    Decode,            // 解码失败
    Backend,           // 后端设备错误（SDL/Audio/GPU）
    Timeout,
    Cancelled,         // 被 Abort/Close 打断（不是错误，是正常退出路径）
    Internal,
};

const char* ToString(StatusCode code) noexcept;

// 轻量错误对象：可拷贝、可比较、带上下文消息。
// 为什么不用异常？播放器里大量"可预期失败"（文件缺失、解码不支持），
// 异常会把控制流藏起来；返回值 + nodiscard 更直白。
class Status
{
public:
    Status() = default;

    static Status Ok() { return Status{}; }

    static Status Error(StatusCode code, std::string message, int systemCode = 0)
    {
        Status s;
        s.code_       = code;
        s.message_    = std::move(message);
        s.systemCode_ = systemCode;
        return s;
    }

    bool ok() const noexcept { return code_ == StatusCode::Ok; }
    explicit operator bool() const noexcept { return ok(); }

    StatusCode         code() const noexcept { return code_; }
    const std::string& message() const noexcept { return message_; }
    int                systemCode() const noexcept { return systemCode_; }

    std::string ToString() const;

private:
    StatusCode  code_ = StatusCode::Ok;
    std::string message_;
    int         systemCode_ = 0;
};

// 简单 Result<T>：要求 T 可默认构造（本项目里都是轻量类型）
template <class T>
class Result
{
public:
    Result(T value) : status_(Status::Ok()), value_(std::move(value)) {}
    Result(Status status) : status_(std::move(status)) {}

    bool           ok() const noexcept { return status_.ok(); }
    explicit       operator bool() const noexcept { return ok(); }
    const Status&  status() const noexcept { return status_; }
    T&             value() { return value_; }
    const T&       value() const { return value_; }
    T&&            MoveValue() { return std::move(value_); }
    T*             operator->() { return &value_; }
    T&             operator*() { return value_; }

private:
    Status status_;
    T      value_{};
};

} // namespace av::core