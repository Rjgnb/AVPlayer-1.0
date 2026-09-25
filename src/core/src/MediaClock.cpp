#include "av/core/MediaClock.h"

#include <algorithm>

namespace av::core {

namespace {
constexpr double kMinSpeed = 0.05;
constexpr double kMaxSpeed = 16.0;
// 文件作用域的默认时钟：在 main 之前初始化，任何线程调用都安全。
// 写成函数内 static 会在"第一次调用者"的线程里做延迟初始化，那个线程往往是
// 音频后台线程，属于隐蔽的线程安全问题。
const SteadyClock kDefaultWallClock;
}

MediaClock::MediaClock(const IClock* wall) : wallClock_(wall)
{
    if (wallClock_ == nullptr)
    {
        wallClock_ = &kDefaultWallClock;
    }
}

void MediaClock::SetSource(Source source)
{
    std::lock_guard<std::mutex> lock(mutex_);
    source_ = source;
}

MediaClock::Source MediaClock::GetSource() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return source_;
}

void MediaClock::SetSpeed(double speed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    speed_ = std::clamp(speed, kMinSpeed, kMaxSpeed);
}

double MediaClock::Speed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return speed_;
}

void MediaClock::SetByteRate(double bytesPerSecond)
{
    std::lock_guard<std::mutex> lock(mutex_);
    byteRate_ = bytesPerSecond > 0.0 ? bytesPerSecond : 0.0;
}

double MediaClock::ByteRate() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return byteRate_;
}

void MediaClock::Reset(double mediaSeconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    baseMedia_    = mediaSeconds > 0.0 ? mediaSeconds : 0.0;
    lastAudioPts_ = baseMedia_;
    hasAudio_     = false;
    wallMedia_    = baseMedia_;
    wallAnchor_   = wallClock_->NowSeconds();
    ended_        = false;
}

void MediaClock::Pause(bool paused)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (paused_ == paused) return;
    if (!paused)
    {
        // 恢复：重新锚定墙钟，避免"暂停期间的时间"被算进去
        wallAnchor_ = wallClock_->NowSeconds();
    }
    paused_ = paused;
}

bool MediaClock::IsPaused() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}

double MediaClock::TickWall()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const double now = wallClock_->NowSeconds();
    const double delta = now - wallAnchor_;
    wallAnchor_ = now;
    if (!paused_ && delta > 0.0)
    {
        wallMedia_ += delta * speed_;
    }
    return wallMedia_;
}

void MediaClock::NotifyAudioWritten(double ptsSeconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lastAudioPts_ = ptsSeconds;
    hasAudio_     = true;
}

double MediaClock::LastAudioPts() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastAudioPts_;
}

double MediaClock::AudioNow(std::size_t queuedBytes) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasAudio_ || byteRate_ <= 0.0) return wallMedia_;
    const double queued = static_cast<double>(queuedBytes) * speed_ / byteRate_;
    const double now    = lastAudioPts_ - queued;
    return now > baseMedia_ ? now : baseMedia_;
}

double MediaClock::WallNow() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return wallMedia_;
}

double MediaClock::Now(std::size_t queuedBytes) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (source_ == Source::Audio && hasAudio_ && byteRate_ > 0.0)
    {
        const double queued = static_cast<double>(queuedBytes) * speed_ / byteRate_;
        const double now    = lastAudioPts_ - queued;
        return now > baseMedia_ ? now : baseMedia_;
    }
    return wallMedia_;
}

double MediaClock::WatermarkBytes(double mediaSeconds) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (byteRate_ <= 0.0 || speed_ <= 0.0) return 0.0;
    return mediaSeconds * byteRate_ / speed_;
}

double MediaClock::QueuedSeconds(std::size_t queuedBytes) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (byteRate_ <= 0.0) return 0.0;
    return static_cast<double>(queuedBytes) * speed_ / byteRate_;
}

void MediaClock::MarkEnded(bool ended)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ended_ = ended;
}

bool MediaClock::IsEnded() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ended_;
}

} // namespace av::core
