#include "TestHarness.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "av/core/BoundedQueue.h"

using namespace av;

using namespace std::chrono_literals;

AV_TEST(BoundedQueue_基本进出顺序)
{
    core::BoundedQueue<int> queue(4);
    AV_CHECK(queue.Push(1));
    AV_CHECK(queue.Push(2));
    AV_CHECK_EQ(queue.Size(), std::size_t(2));

    int value = 0;
    AV_CHECK(queue.TryTake(value));
    AV_CHECK_EQ(value, 1);
    AV_CHECK(queue.TryTake(value));
    AV_CHECK_EQ(value, 2);
    AV_CHECK_EQ(queue.Take(value, 1ms), core::TakeResult::Timeout);
}

AV_TEST(BoundedQueue_Close_允许取完剩下的数据)
{
    core::BoundedQueue<int> queue(4);
    queue.Push(7);
    queue.Close();

    AV_CHECK(!queue.Push(8));          // 关了就收不了
    int value = 0;
    AV_CHECK(queue.TryTake(value));    // 但剩下的还能取
    AV_CHECK_EQ(value, 7);
    AV_CHECK_EQ(queue.Take(value, 1ms), core::TakeResult::Closed);
}

AV_TEST(BoundedQueue_Abort_立刻失败并丢弃数据)
{
    core::BoundedQueue<std::shared_ptr<int>> queue(4);
    auto payload = std::make_shared<int>(42);
    std::weak_ptr<int> observer = payload;

    queue.Push(std::move(payload));
    queue.Abort();

    std::shared_ptr<int> out;
    AV_CHECK_EQ(queue.Take(out, 1ms), core::TakeResult::Closed);
    AV_CHECK(observer.expired());       // Abort 时必须释放元素（老版本这里会漏）
    AV_CHECK(!queue.Push(std::make_shared<int>(1)));
}

AV_TEST(BoundedQueue_Reset_可复用_用于seek的flush)
{
    core::BoundedQueue<int> queue(4);
    queue.Push(1);
    queue.Push(2);
    queue.Reset();                     // seek：清空并重新打开

    AV_CHECK_EQ(queue.Size(), std::size_t(0));
    AV_CHECK(queue.Push(3));           // 重置后可以继续用
    int value = 0;
    AV_CHECK(queue.TryTake(value));
    AV_CHECK_EQ(value, 3);
}

AV_TEST(BoundedQueue_按谓词从队首丢弃前缀)
{
    core::BoundedQueue<int> queue(4);
    queue.Push(1);
    queue.Push(2);
    queue.Push(3);
    queue.Push(4);

    AV_CHECK_EQ(queue.DropFrontWhile([](const int& value) { return value < 3; }), std::size_t(2));
    AV_CHECK_EQ(queue.Size(), std::size_t(2));

    int value = 0;
    AV_CHECK(queue.TryTake(value));
    AV_CHECK_EQ(value, 3);              // 第一个不满足条件的元素必须保留
}

AV_TEST(BoundedQueue_满时阻塞_Abort后能醒来)
{
    core::BoundedQueue<int> queue(1);
    queue.Push(1);

    std::atomic<bool> pushReturned{ false };
    std::atomic<bool> pushResult{ true };
    std::thread producer([&] {
        pushResult = queue.Push(2);    // 队列满 -> 阻塞在这里
        pushReturned = true;
    });

    std::this_thread::sleep_for(20ms);
    AV_CHECK(!pushReturned.load());    // 确实被挡住了（这就是背压）

    queue.Abort();
    producer.join();
    AV_CHECK(pushReturned.load());
    AV_CHECK(!pushResult.load());      // 被 Abort 叫醒后返回 false
}
