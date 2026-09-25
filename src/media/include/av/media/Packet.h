#pragma once

#include "av/media/FFmpegCompat.h"

#include "av/core/Rational.h"

namespace av::media {

// AVPacket 的 RAII 包装：拷贝被禁用、移动合法，
// 配合 BoundedQueue 的 deleter 使用就再也不会漏包。
class Packet
{
public:
    Packet() { packet_ = av_packet_alloc(); }
    ~Packet() { av_packet_free(&packet_); }

    Packet(Packet&& other) noexcept : packet_(other.packet_) { other.packet_ = nullptr; }
    Packet& operator=(Packet&& other) noexcept
    {
        if (this != &other)
        {
            av_packet_free(&packet_);
            packet_       = other.packet_;
            other.packet_ = nullptr;
        }
        return *this;
    }

    Packet(const Packet&)            = delete;
    Packet& operator=(const Packet&) = delete;

    bool IsAllocated() const noexcept { return packet_ != nullptr; }

    // 释放引用但保留容器，用于复用
    void Reset()
    {
        if (packet_) av_packet_unref(packet_);
    }

    int StreamIndex() const noexcept { return packet_ ? packet_->stream_index : -1; }

    double PtsSeconds(core::Rational timeBase) const noexcept
    {
        if (packet_ == nullptr || packet_->pts == AV_NOPTS_VALUE) return -1.0;
        return core::ToSeconds(packet_->pts, timeBase);
    }

    double DurationSeconds(core::Rational timeBase) const noexcept
    {
        if (packet_ == nullptr || packet_->duration <= 0) return 0.0;
        return core::ToSeconds(packet_->duration, timeBase);
    }

    AVPacket*       Raw() noexcept { return packet_; }
    const AVPacket* Raw() const noexcept { return packet_; }

private:
    AVPacket* packet_ = nullptr;
};

// 队列用的回收器：Packet 只有移动语义，deleter 只需 Reset
struct PacketDeleter
{
    void operator()(Packet& packet) const { packet.Reset(); }
};

} // namespace av::media
