#include "av/ui/Overlay.h"

#include "av/core/StringFormat.h"
#include "av/ui/BitmapFont.h"

#include <cmath>
#include <vector>

namespace av::ui {

namespace {

constexpr core::Color kPanelColor   = { 0, 0, 0, 120 };
constexpr core::Color kTrackColor   = { 255, 255, 255, 70 };
constexpr core::Color kPlayedColor  = { 255, 90, 70, 230 };
constexpr core::Color kKnobColor    = { 255, 255, 255, 240 };
constexpr core::Color kTextColor    = { 255, 255, 255, 235 };
constexpr core::Color kShadowColor  = { 0, 0, 0, 160 };

void AppendProgressBar(const OverlayState& state, std::vector<core::Rect>& panel,
                       std::vector<core::Rect>& track, std::vector<core::Rect>& played,
                       std::vector<core::Rect>& knob)
{
    if (state.bar.width <= 0 || state.bar.height <= 0) return;

    track.push_back(state.bar);

    const double ratio = state.duration > 0.0 ? state.position / state.duration : 0.0;
    const double clamped = ratio < 0.0 ? 0.0 : (ratio > 1.0 ? 1.0 : ratio);
    const int playedWidth = static_cast<int>(state.bar.width * clamped);

    if (playedWidth > 0)
    {
        played.push_back(core::Rect{ state.bar.x, state.bar.y, playedWidth, state.bar.height });
    }

    // 拖拽把手：一个矮宽的方块（比画圆简单，视觉上也够清楚）
    constexpr int knobWidth = 10;
    knob.push_back(core::Rect{
        state.bar.x + playedWidth - knobWidth / 2,
        state.bar.y - 5,
        knobWidth,
        state.bar.height + 10 });
    (void)panel;
}

} // namespace

void OverlayRenderer::Draw(const OverlayState& state, output::IRenderTarget& target)
{
    if (!state.visible) return;

    const core::Size size = target.Size();
    if (size.width <= 0 || size.height <= 0) return;

    std::vector<core::Rect> panel;
    std::vector<core::Rect> track;
    std::vector<core::Rect> played;
    std::vector<core::Rect> knob;
    std::vector<core::Rect> shadow;
    std::vector<core::Rect> text;

    // 底部信息条（从进度条上方 34px 一直铺到底）
    const int panelTop = state.bar.y - 34;
    if (panelTop > 0)
    {
        panel.push_back(core::Rect{ 0, panelTop, size.width, size.height - panelTop });
    }

    AppendProgressBar(state, panel, track, played, knob);

    // 时间文字：00:12 / 01:23
    const int scale = size.height >= 480 ? 2 : 1;
    const std::string timeText = core::FormatTimecode(state.position) + " / " + core::FormatTimecode(state.duration);
    const core::Point timeOrigin{ state.bar.x, state.bar.y - BitmapFont::LineHeight(scale) - 12 };

    BitmapFont::AppendTextRects(timeText, core::Point{ timeOrigin.x + scale, timeOrigin.y + scale }, scale, shadow);
    BitmapFont::AppendTextRects(timeText, timeOrigin, scale, text);

    // 倍速（只在非 1.0x 时显示）
    if (std::abs(state.speed - 1.0) > 1e-3)
    {
        const std::string speedText = core::FormatSpeed(state.speed);
        const int width = BitmapFont::TextWidth(speedText, scale);
        const core::Point origin{ state.bar.x + state.bar.width - width, timeOrigin.y };
        BitmapFont::AppendTextRects(speedText, core::Point{ origin.x + scale, origin.y + scale }, scale, shadow);
        BitmapFont::AppendTextRects(speedText, origin, scale, text);
    }

    if (!panel.empty())  target.FillRects(panel.data(), panel.size(), kPanelColor);
    if (!track.empty())  target.FillRects(track.data(), track.size(), kTrackColor);
    if (!played.empty()) target.FillRects(played.data(), played.size(), kPlayedColor);
    if (!knob.empty())   target.FillRects(knob.data(), knob.size(), kKnobColor);
    if (!shadow.empty()) target.FillRects(shadow.data(), shadow.size(), kShadowColor);
    if (!text.empty())   target.FillRects(text.data(), text.size(), kTextColor);

    // 暂停时在画面中央画两条竖杠（"暂停"图标）—— 用矩形就够了
    if (!state.playing)
    {
        const int centerX = size.width / 2;
        const int centerY = size.height / 2;
        const int barWidth = size.width / 40 + 6;
        const int barHeight = size.height / 6 + 20;
        const core::Rect bars[2] = {
            core::Rect{ centerX - barWidth * 2, centerY - barHeight / 2, barWidth, barHeight },
            core::Rect{ centerX + barWidth,     centerY - barHeight / 2, barWidth, barHeight },
        };
        target.FillRects(bars, 2, kKnobColor);
    }
}

} // namespace av::ui