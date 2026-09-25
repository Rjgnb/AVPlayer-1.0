#pragma once

#include <vector>

#include "SharedState.h"

namespace av::detail {

// 音频线程：包 -> 解码 -> 重采样 -> 设备 -> 更新时钟
//
// 为什么"解码 + 输出"放在同一条线程？
// 因为音频的节奏由设备水位决定（写不进去就等），拆成两条只会多一个队列和一次拷贝，
// 却换不来任何并行收益。视频则相反：解码很贵、呈现要等时钟，必须分开。
class AudioPipeline
{
public:
    explicit AudioPipeline(SharedStatePtr state) : state_(std::move(state)) {}

    void Run();

private:
    void WriteFrames(std::vector<media::Frame>& frames, double& dropBefore);
    bool WaitForRoom(double watermarkBytes);

    SharedStatePtr state_;
};

} // namespace av::detail