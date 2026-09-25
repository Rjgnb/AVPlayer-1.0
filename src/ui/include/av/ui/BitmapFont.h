#pragma once

#include <string_view>
#include <vector>

#include "av/core/Types.h"

namespace av::ui {

// 5x7 位图字体 -> 一组矩形。
//
// 为什么不用 SDL_ttf / FreeType？
//   1) 覆盖层只需要"时间 + 倍速"这几个字符，引入字体库会把后端依赖搞复杂；
//   2) 矩形是 IRenderTarget 的通用原语 —— 同一份文字在 SDL / Qt / 内存录制器上都能画。
// 想加字符：在 BitmapFont.cpp 的 kGlyphs 表里加一行即可。
class BitmapFont
{
public:
    static constexpr int kGlyphWidth    = 5;
    static constexpr int kGlyphHeight   = 7;
    static constexpr int kLetterSpacing = 1;

    // 把 text 拆成矩形追加到 out（同一像素行的连续段会合并成一个矩形）
    static void AppendTextRects(std::string_view text, core::Point origin, int scale, std::vector<core::Rect>& out);

    static int TextWidth(std::string_view text, int scale);
    static int LineHeight(int scale) { return kGlyphHeight * scale; }

    // 便于测试：某个字符是否有字形
    static bool HasGlyph(char ch);
};

} // namespace av::ui