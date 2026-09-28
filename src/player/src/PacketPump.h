#pragma once

#include "SharedState.h"

namespace av::detail {

// demux 线程：把"容器"变成"两条包队列"。
// 它也负责执行 seek —— 因为 seek 必须和 av_read_frame 在**同一条线程**里做，
// 否则就要给 AVFormatContext 到处加锁（老代码正是这么踩坑的）。
//
// 数据流位置：
//   Demuxer -> PacketPump -> audioPackets / videoPackets -> 两条解码 pipeline
class PacketPump
{
public:
    explicit PacketPump(SharedStatePtr state) : state_(std::move(state)) {}

    void Run();

private:
    void HandleSeekRequest();

    SharedStatePtr state_;
};

} // namespace av::detail
