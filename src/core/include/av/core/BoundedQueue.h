#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <ostream>
#include <utility>

namespace av::core {

enum class TakeResult
{
    Ok,
    Timeout,
    Closed,   // 队列被 Abort，或已 Close 且取空
};

inline const char* ToString(TakeResult result) noexcept
{
    switch (result)
    {
    case TakeResult::Ok:      return "Ok";
    case TakeResult::Timeout: return "Timeout";
    case TakeResult::Closed:  return "Closed";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, TakeResult result) { return os << ToString(result); }

inline constexpr std::chrono::milliseconds kForever{ std::chrono::milliseconds::max() };

// 有界阻塞队列（生产者/消费者解耦的核心原语）
//
// 三种"结束"语义，互不混淆（老版本只有一个不可逆的 close()，seek 时无法清空）：
//   Close()  正常收尾：不再接受 Push，但消费者可以把剩余数据取完
//   Abort()  立刻放弃：丢弃所有数据，之后 Take/Push 立即失败（用于停止/销毁）
//   Reset()  重置复用：清空数据并重新打开（用于 seek 的 flush）
//
// SetDeleter() 保证"丢弃"时也走析构逻辑 —— 队列里放 RAII 资源（AVPacket/AVFrame）时
// 不设置它就是内存泄漏（老代码 seek 时正是这么漏的）。
template <class T>
class BoundedQueue
{
public:
    using Deleter = std::function<void(T&)>;

    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    ~BoundedQueue() { Clear(); }

    BoundedQueue(const BoundedQueue&)            = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    void SetDeleter(Deleter deleter)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        deleter_ = std::move(deleter);
    }

    // 满时阻塞；返回 false 表示队列已关闭/已放弃（生产者应当退出）
    bool Push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] { return closed_ || aborted_ || !IsFullLocked(); });
        if (closed_ || aborted_) return false;
        items_.push_back(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    // 满时最多等 timeout；返回 false = "没放进去"（超时 / 已关闭 / 已放弃）。
    //
    // 为什么需要它（这是本项目踩过的真实死锁）：
    //   "暂停 -> 拖进度条" 时，主线程不再从 videoFrames 取帧，于是视频解码线程
    //   永远卡在 Push 里；demux 线程又卡在 videoPackets 里 —— 而 seek 请求要由
    //   demux 线程来执行，结果谁也走不动。
    //   把"无限等"换成"等一小会儿就回来看看控制位"，生产者就永远能响应 seek/退出。
    //
    // 注意：**失败时不会消费 value**，调用方可以拿同一个对象重试。
    //
    bool PushWaiting(T& value, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool signaled =
            notFull_.wait_for(lock, timeout, [this] { return closed_ || aborted_ || !IsFullLocked(); });
        if (!signaled || closed_ || aborted_) return false;
        items_.push_back(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    TakeResult Take(T& out, std::chrono::milliseconds timeout = kForever)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool signaled = notEmpty_.wait_for(lock, timeout, [this] {
            return aborted_ || !items_.empty() || closed_;
        });
        if (!signaled) return TakeResult::Timeout;
        if (!items_.empty())
        {
            out = std::move(items_.front());
            items_.pop_front();
            notFull_.notify_one();
            return TakeResult::Ok;
        }
        return TakeResult::Closed;   // aborted，或 closed 且已取空
    }

    // 非阻塞："有就拿走，没有立刻返回 false"（try_pop 语义）。
    // 刻意不叫 Take 的重载：Take(x) 会在"默认超时"和"非阻塞"之间产生重载歧义。
    bool TryTake(T& out) { return Take(out, std::chrono::milliseconds(0)) == TakeResult::Ok; }

    void Close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void Abort()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        aborted_ = true;
        DropLocked();
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DropLocked();
        closed_  = false;
        aborted_ = false;
        notFull_.notify_all();
    }

    // 从队首连续丢弃满足 predicate 的元素，直到第一个不满足者为止。
    // 典型用途：暂停 seek 预览时，消费端暂时不能播放旧的音频包，但 demux
    // 线程不能被小队列卡死；丢掉“目标之前的包”，保留目标及之后的包。
    template <class Predicate>
    std::size_t DropFrontWhile(Predicate predicate)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t dropped = 0;
        while (!items_.empty() && predicate(static_cast<const T&>(items_.front())))
        {
            if (deleter_) deleter_(items_.front());
            items_.pop_front();
            ++dropped;
        }
        if (dropped != 0) notFull_.notify_all();
        return dropped;
    }

    // 清空但不改变状态
    void Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DropLocked();
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    bool IsEmpty() const { return Size() == 0; }

    bool IsClosed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    bool IsAborted() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return aborted_;
    }

    std::size_t Capacity() const noexcept { return capacity_; }

private:
    bool IsFullLocked() const { return capacity_ != 0 && items_.size() >= capacity_; }

    void DropLocked()
    {
        if (deleter_)
        {
            for (auto& item : items_) deleter_(item);
        }
        items_.clear();
    }

    mutable std::mutex      mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<T>           items_;
    std::size_t             capacity_ = 0;   // 0 = 无界
    bool                    closed_   = false;
    bool                    aborted_  = false;
    Deleter                 deleter_;
};

} // namespace av::core
