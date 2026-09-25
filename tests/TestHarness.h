#pragma once

// 极简测试框架（不引第三方）：够用、可读、零依赖。
//   写测试：  AV_TEST(名字) { AV_CHECK(...); }
//   跑测试：  av_tests [名字过滤]
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "av/core/Types.h"
#include "av/output/RenderTarget.h"

namespace avtest {

class Failure : public std::runtime_error
{
public:
    explicit Failure(const std::string& message) : std::runtime_error(message) {}
};

struct TestCase
{
    std::string           name;
    std::function<void()> body;
};

std::vector<TestCase>& AllTests();
void                   AddTest(std::string name, std::function<void()> body);
[[noreturn]] void      Fail(const char* file, int line, const std::string& message);

struct Registrar
{
    Registrar(std::string name, std::function<void()> body) { AddTest(std::move(name), std::move(body)); }
};

// 记录绘制调用的假渲染目标：用来对"画在哪儿"做断言（UI 单测的关键道具）。
// 它实现的是真正的 output::IRenderTarget —— 所以单测里验证的就是生产接口本身，
// 不存在"测试用另一套签名"的偏差。
class RecordingRenderTarget final : public av::output::IRenderTarget
{
public:
    explicit RecordingRenderTarget(int width, int height) : size_{ width, height } {}

    av::core::Size Size() const override { return size_; }
    void FillRects(const av::core::Rect* rects, std::size_t count, av::core::Color color) override
    {
        for (std::size_t i = 0; i < count; ++i) { filled.push_back(rects[i]); fillColors.push_back(color); }
    }
    void DrawLines(const av::core::Line* lines, std::size_t count, av::core::Color color, int thickness) override
    {
        (void)color;
        (void)thickness;
        for (std::size_t i = 0; i < count; ++i) drawn.push_back(lines[i]);
    }

    std::vector<av::core::Rect> filled;
    std::vector<av::core::Color> fillColors;
    std::vector<av::core::Line> drawn;

private:
    av::core::Size size_;
};

} // namespace avtest

#define AV_TEST(name)                                                        \
    static void name();                                                      \
    static const ::avtest::Registrar avtest_registrar_##name(#name, name);   \
    static void name()

#define AV_CHECK(condition)                                                  \
    do {                                                                     \
        if (!(condition)) ::avtest::Fail(__FILE__, __LINE__, "断言失败: " #condition); \
    } while (0)

#define AV_CHECK_EQ(actual, expected)                                        \
    do {                                                                     \
        const auto& av_actual   = (actual);                                  \
        const auto& av_expected = (expected);                                \
        if (!(av_actual == av_expected)) {                                   \
            std::ostringstream av_oss;                                       \
            av_oss << "期望 " #actual " == " #expected "，实际得到 " << av_actual; \
            ::avtest::Fail(__FILE__, __LINE__, av_oss.str());                \
        }                                                                    \
    } while (0)

#define AV_CHECK_NEAR(actual, expected, tolerance)                           \
    do {                                                                     \
        const double av_actual   = (actual);                                 \
        const double av_expected = (expected);                               \
        const double av_delta    = av_actual - av_expected;                  \
        if (!(av_delta < (tolerance) && av_delta > -(tolerance))) {          \
            std::ostringstream av_oss;                                       \
            av_oss << "期望 " #actual " ≈ " << av_expected << " (±" << (tolerance) << ")，实际 " << av_actual; \
            ::avtest::Fail(__FILE__, __LINE__, av_oss.str());                \
        }                                                                    \
    } while (0)
