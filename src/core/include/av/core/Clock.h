#pragma once

#include <chrono>

namespace av::core {

// 墙钟抽象：让"时间"变成可注入的依赖 —— 测试里用假时钟，不用 sleep
class IClock
{
public:
    virtual ~IClock() = default;
    virtual double NowSeconds() const = 0;   // 单调递增，单位秒
};

// 默认实现：std::chrono::steady_clock（不受系统时间调整影响）
class SteadyClock final : public IClock
{
public:
    double NowSeconds() const override
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration<double>(now).count();
    }
};

// 手动时钟：单元测试用
class ManualClock final : public IClock
{
public:
    double NowSeconds() const override { return now_; }
    void   Advance(double seconds) { now_ += seconds; }
    void   SetNow(double seconds) { now_ = seconds; }

private:
    double now_ = 0.0;
};

} // namespace av::core