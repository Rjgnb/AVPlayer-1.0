#pragma once

#include <vector>

#include "SharedState.h"

namespace av::detail {

// 视频解码线程：包 -> 解码 -> 带世代号入帧队列
// 它**不做同步**：什么时候显示由主线程按时钟决定（解码与节流分离，才能并行）
//
// 数据流位置：
//   videoPackets -> Decoder -> videoFrames(Tagged<Frame>) -> Player::PresentDueFrame
class VideoPipeline
{
public:
    explicit VideoPipeline(SharedStatePtr state) : state_(std::move(state)) {}

    void Run();

private:
    void PushFrames(std::vector<media::Frame>& frames, core::Generation generation, double& dropBefore);

    SharedStatePtr state_;
};

} // namespace av::detail
