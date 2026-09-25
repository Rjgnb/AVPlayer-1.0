#pragma once

#include "av/media/FFmpegCompat.h"

#include <vector>

#include "av/core/Status.h"
#include "av/media/Frame.h"
#include "av/media/MediaInfo.h"
#include "av/media/Packet.h"

namespace av::media {

// 音频/视频解码器共用一个实现：avcodec_send_packet / avcodec_receive_frame
// 本身就是同一套状态机，"按类抄两份"只会让修 bug 要改两处。
class Decoder
{
public:
    Decoder() = default;
    ~Decoder();

    Decoder(Decoder&& other) noexcept;
    Decoder& operator=(Decoder&& other) noexcept;
    Decoder(const Decoder&)            = delete;
    Decoder& operator=(const Decoder&) = delete;

    core::Status Open(AVStream* stream, int threads = 0);
    void         Close();
    bool         IsOpen() const noexcept { return codec_ != nullptr; }

    AVMediaType MediaType() const noexcept { return mediaType_; }
    std::string CodecName() const;

    // 送包 -> 取帧。packet 为 nullptr 表示"喂完输入，开始 drain"（EOF 收尾）。
    // out 里可能有 0..N 帧（B 帧会让一次输入产出多帧）。
    core::Status Decode(const Packet* packet, std::vector<Frame>& out);

    // seek 之后必须调用：丢掉解码器内部缓存的参考帧
    void Flush();

    // 是否已经 drain 完毕（用于 EOF 判断）
    bool IsDrained() const noexcept { return drained_; }

private:
    AVCodecContext* codec_     = nullptr;
    AVMediaType     mediaType_ = AVMEDIA_TYPE_UNKNOWN;
    bool            draining_  = false;
    bool            drained_   = false;
};

} // namespace av::media
