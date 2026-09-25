#pragma once

// ---------------------------------------------------------------------------
// 【FFmpeg 边界】允许出现 FFmpeg 头文件的唯一入口
//
// 这一层要处理两个真实存在的坑，集中处理一次就够了：
//
//   1) 本 SDK 的 FFmpeg 头文件**没有 `extern "C"` 保护**。
//      C++ 里直接 #include 会让编译器把 av_* 当成 C++ 函数（符号被 mangle 成
//      ?av_packet_alloc@@YA...），而 DLL 导出的是 C 名字 av_packet_alloc，
//      链接期必然 LNK2019。=> 统一在外面包一层 extern "C"。
//
//   2) libavutil/common.h 在 C++ 下要求 __STDC_CONSTANT_MACROS 先就位，否则 #error。
//
// 于是规矩只有一条：**项目里其它文件不要直接 #include <libav*/libsw*>，只包这个头。**
// 换来的是：以后换 SDK、换 FFmpeg 大版本（API 改名/新增头文件）只改这一个地方。
// ---------------------------------------------------------------------------

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <cstdint>

#if defined(__cplusplus)
extern "C" {
#endif

// ---- libavutil ----
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>

// ---- libavcodec ----
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>

// ---- libavformat ----
#include <libavformat/avformat.h>

// ---- libswresample / libswscale ----
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#if defined(__cplusplus)
}   // extern "C"
#endif