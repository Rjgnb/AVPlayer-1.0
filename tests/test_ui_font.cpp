#include "TestHarness.h"

#include <vector>

#include "av/ui/BitmapFont.h"

using namespace av;

AV_TEST(BitmapFont_字形覆盖与宽度)
{
    AV_CHECK(av::ui::BitmapFont::HasGlyph('0'));
    AV_CHECK(av::ui::BitmapFont::HasGlyph('9'));
    AV_CHECK(av::ui::BitmapFont::HasGlyph(':'));
    AV_CHECK(av::ui::BitmapFont::HasGlyph('A'));
    AV_CHECK(av::ui::BitmapFont::HasGlyph('X'));
    AV_CHECK(!av::ui::BitmapFont::HasGlyph('~'));   // 未收录：不崩，只是不画

    AV_CHECK_EQ(av::ui::BitmapFont::TextWidth("0", 1), 5);
    AV_CHECK_EQ(av::ui::BitmapFont::TextWidth("00", 1), 11);
    AV_CHECK_EQ(av::ui::BitmapFont::TextWidth("000", 2), 34);
    AV_CHECK_EQ(av::ui::BitmapFont::TextWidth("", 2), 0);
}

AV_TEST(BitmapFont_矩形不越界且合并了连续段)
{
    std::vector<core::Rect> rects;
    av::ui::BitmapFont::AppendTextRects("0", core::Point{ 10, 20 }, 2, rects);

    AV_CHECK(!rects.empty());
    for (const core::Rect& rect : rects)
    {
        AV_CHECK(rect.x >= 10 && rect.x + rect.width <= 10 + 5 * 2);
        AV_CHECK(rect.y >= 20 && rect.y + rect.height <= 20 + 7 * 2);
        AV_CHECK_EQ(rect.height, 2);          // 每段正好一个 scale 行高
    }

    // '0' 一共有 19 个亮像素；行内合并后矩形数必须明显少（逐像素画才是 19 个）
    AV_CHECK(rects.size() < 19);

    // 合并的判据：同一行的两段之间必须留空隙 —— 挨着就说明没合并
    for (std::size_t i = 0; i < rects.size(); ++i)
    {
        for (std::size_t j = i + 1; j < rects.size(); ++j)
        {
            if (rects[i].y != rects[j].y) continue;
            const bool separated = rects[i].x + rects[i].width < rects[j].x ||
                                   rects[j].x + rects[j].width < rects[i].x;
            AV_CHECK(separated);
        }
    }
}

AV_TEST(BitmapFont_空串不产生矩形)
{
    std::vector<core::Rect> rects;
    av::ui::BitmapFont::AppendTextRects("", core::Point{ 0, 0 }, 1, rects);
    AV_CHECK(rects.empty());
}
