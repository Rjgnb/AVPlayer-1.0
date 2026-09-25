#pragma once

#include <cstddef>
#include <string>

#include "av/core/KeyValues.h"
#include "av/core/Log.h"

namespace av::core {

// 播放器的强类型配置。后端专有项放 backendOptions（KeyValues），
// 这样"加一个新后端"不需要动这个结构体。
struct PlayerConfig
{
    // ---- 音视频开关 ----
    bool enableAudio = true;
    bool enableVideo = true;

    // ---- 缓冲/水位 ----
    int    audioBufferSamples    = 1024;   // 每次投递给设备的采样数（影响延迟）
    double audioWatermarkSeconds = 0.30;   // 设备排队水位：超过就暂停喂数据（背压）
    std::size_t audioPacketQueueCapacity = 64;
    std::size_t videoPacketQueueCapacity = 64;
    std::size_t videoFrameQueueCapacity  = 16;
    double maxVideoLeadSeconds = 1.0;      // 视频帧最多领先时钟多久（超出就丢帧）

    // ---- 打开参数 ----
    double startSeconds = 0.0;
    bool   loop         = false;           // 播完自动重播

    // ---- 输出后端 ----
    std::string backendName;               // 空 = 用注册表里的默认后端
    std::string windowTitle;
    bool   vsync     = true;
    bool   resizable = true;               // Tick() 在主线程呈现，缩放是安全的
    KeyValues backendOptions;

    // ---- 日志 ----
    LogLevel logLevel = LogLevel::Info;
};

} // namespace av::core