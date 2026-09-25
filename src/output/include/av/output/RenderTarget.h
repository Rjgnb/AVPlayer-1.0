#pragma once

#include <cstddef>

#include "av/core/Types.h"

namespace av::output {

// 覆盖层绘制接口：**只提供最小原语**（矩形 + 线段）。
//
// 为什么要这么"抠"？因为文字可以用位图字体拆成矩形（见 av::ui::BitmapFont）。
// 于是同一套 UI 逻辑可以画在 SDL 纹理上、Qt 的 QPainter 上，也可以在单元测试里
// 用 RecordingRenderTarget 断言"进度条画在哪个像素区间"——不用开窗口。
class IRenderTarget
{
public:
    virtual ~IRenderTarget() = default;

    // 绘制区域尺寸（窗口客户区，像素）
    virtual core::Size Size() const = 0;

    virtual void FillRects(const core::Rect* rects, std::size_t count, core::Color color) = 0;
    virtual void DrawLines(const core::Line* lines, std::size_t count, core::Color color, int thickness = 1) = 0;
};

} // namespace av::output