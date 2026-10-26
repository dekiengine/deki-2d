// The gradient bake.
//
// RenderToBuffer reuses the last pixel's colour and fills runs instead of
// working every pixel out from scratch. These pin it to the bake worked out
// pixel by pixel, for every type, tiling and dither, at the art's size and
// bigger and smaller than it; and check the one-column and one-row strips a
// gradient along an axis is baked as.

#include <gtest/gtest.h>

#include <GradientComponent.h>
#include "PixelFormat.h"

#include <deki/Engine.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace Deki2D;

namespace
{

const uint8_t kBayer2[4] = { 0, 2, 3, 1 };
const uint8_t kBayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

// The bake worked out every pixel on its own. Only the 2x2 and 4x4
// dithers are here, which is enough to reach every branch.
std::vector<uint16_t> PixelByPixel(const GradientComponent& g, int32_t outW, int32_t outH, int32_t artW, int32_t artH,
                                   int32_t ditherArt)
{
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    float cosA = cosf(g.angle);
    float sinA = sinf(g.angle);
    // A whole number of right angles is drawn exactly straight.
    if (std::fabs(cosA) < 1e-6f)
    {
        cosA = 0.0f;
        sinA = sinA < 0.0f ? -1.0f : 1.0f;
    }
    else if (std::fabs(sinA) < 1e-6f)
    {
        sinA = 0.0f;
        cosA = cosA < 0.0f ? -1.0f : 1.0f;
    }
    const int32_t tileWPx = static_cast<int32_t>(g.tileWidth * ppm);
    const int32_t tileHPx = static_cast<int32_t>(g.tileHeight * ppm);
    const int32_t tw = tileWPx > 0 ? tileWPx : artW;
    const int32_t th = tileHPx > 0 ? tileHPx : artH;
    const bool ditherOnArt = outW >= artW && outH >= artH;
    const int32_t outCell =
        std::max<int32_t>(1, static_cast<int32_t>(std::lround(static_cast<double>(ditherArt) * outW / artW)));

    auto position = [&](float nx, float ny) -> float
    {
        switch (g.gradientType)
        {
            case GradientType::Linear:
            {
                const float p = nx * cosA + ny * sinA;
                const float lo = std::min(0.0f, cosA) + std::min(0.0f, sinA);
                const float hi = std::max(0.0f, cosA) + std::max(0.0f, sinA);
                return std::clamp((p - lo) / (hi - lo), 0.0f, 1.0f);
            }
            case GradientType::Radial:
            {
                const float dx = nx - g.centerX;
                const float dy = ny - g.centerY;
                return std::clamp(sqrtf(dx * dx + dy * dy) / g.radius, 0.0f, 1.0f);
            }
            case GradientType::Conical:
            {
                const float a = atan2f(ny - g.centerY, nx - g.centerX) + 3.14159265358979323846;
                return a / (2.0f * 3.14159265358979323846);
            }
        }
        return nx;
    };

    auto blend = [&](float p) -> Deki::Color
    {
        const int n = g.stopCount;
        p = std::clamp(p, 0.0f, 1.0f);
        if (n == 1 || p <= g.stops[0].position)
        {
            return g.stops[0].color;
        }
        if (p >= g.stops[n - 1].position)
        {
            return g.stops[n - 1].color;
        }
        for (int i = 0; i < n - 1; i++)
        {
            if (p >= g.stops[i].position && p <= g.stops[i + 1].position)
            {
                const float t = (p - g.stops[i].position) / (g.stops[i + 1].position - g.stops[i].position);
                const Deki::Color& a = g.stops[i].color;
                const Deki::Color& b = g.stops[i + 1].color;
                return Deki::Color((uint8_t)(a.r + t * (b.r - a.r)), (uint8_t)(a.g + t * (b.g - a.g)),
                                   (uint8_t)(a.b + t * (b.b - a.b)));
            }
        }
        return Deki::Color(0, 0, 0);
    };

    auto pick = [&](float p, float threshold) -> Deki::Color
    {
        const int n = g.stopCount;
        if (n == 1 || p < g.stops[0].position)
        {
            return g.stops[0].color;
        }
        if (p >= g.stops[n - 1].position)
        {
            return g.stops[n - 1].color;
        }
        for (int i = 0; i < n - 1; i++)
        {
            if (p >= g.stops[i].position && p < g.stops[i + 1].position)
            {
                const float range = g.stops[i + 1].position - g.stops[i].position;
                const float t = range > 0.0f ? (p - g.stops[i].position) / range : 0.0f;
                return t >= threshold ? g.stops[i + 1].color : g.stops[i].color;
            }
        }
        return Deki::Color(0, 0, 0);
    };

    auto norm = [&](int32_t i, int32_t full, int32_t tile, bool tiled) -> float
    {
        if (g.tileMode == GradientTileMode::Mirror)
        {
            const int32_t t = i % (tile * 2);
            return t < tile ? (float)t / tile : 1.0f - (float)(t - tile) / tile;
        }
        return tiled ? (float)(i % tile) / tile : (float)i / full;
    };
    const bool across = g.tileMode == GradientTileMode::Horizontal || g.tileMode == GradientTileMode::Both;
    const bool down = g.tileMode == GradientTileMode::Vertical || g.tileMode == GradientTileMode::Both;

    std::vector<uint16_t> out(static_cast<size_t>(outW) * outH);
    for (int32_t oy = 0; oy < outH; oy++)
    {
        const int32_t y = static_cast<int32_t>((static_cast<int64_t>(oy) * artH) / outH);
        const int32_t cellY = ditherOnArt ? y / ditherArt : oy / outCell;
        for (int32_t ox = 0; ox < outW; ox++)
        {
            const int32_t x = static_cast<int32_t>((static_cast<int64_t>(ox) * artW) / outW);
            const float p = position(norm(x, artW, tw, across), norm(y, artH, th, down));
            Deki::Color c;
            if (g.ditherMode == GradientDitherMode::None)
            {
                c = blend(p);
            }
            else
            {
                const int32_t cellX = ditherOnArt ? x / ditherArt : ox / outCell;
                const float threshold = g.ditherMode == GradientDitherMode::Ordered2x2
                                            ? kBayer2[(cellY & 1) * 2 + (cellX & 1)] / 4.0f
                                            : kBayer4[(cellY & 3) * 4 + (cellX & 3)] / 16.0f;
                c = pick(p, threshold);
            }
            const uint16_t rgb = ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
            out[static_cast<size_t>(oy) * outW + ox] = DekiPixel::ToMemory565(rgb);
        }
    }
    return out;
}

void SetThreeStops(GradientComponent& g)
{
    g.stopCount = 3;
    g.stop1Position = 0.1f;
    g.stop1Color = Deki::Color(250, 40, 10);
    g.stop2Position = 0.55f;
    g.stop2Color = Deki::Color(20, 200, 90);
    g.stop3Position = 0.9f;
    g.stop3Color = Deki::Color(30, 60, 240);
}

// Bakes `g` at every size and dither cell below and compares each with the
// pixel-by-pixel bake.
void ExpectSameAsPixelByPixel(GradientComponent& g)
{
    struct Size
    {
        int32_t outW, outH, artW, artH;
    };
    // At the art's size, a whole-number upscale, an uneven one, and smaller.
    const Size sizes[] = { { 96, 64, 96, 64 }, { 192, 128, 96, 64 }, { 131, 77, 96, 64 }, { 41, 29, 96, 64 } };
    for (const Size& s : sizes)
    {
        for (int32_t ditherArt : { 1, 2, 4 })
        {
            std::vector<uint16_t> baked(static_cast<size_t>(s.outW) * s.outH, 0xDEAD);
            ASSERT_TRUE(
                g.RenderToBuffer(reinterpret_cast<uint8_t*>(baked.data()), s.outW, s.outH, s.artW, s.artH, ditherArt));
            // RenderToBuffer copies the stopN properties into stops[] first.
            const std::vector<uint16_t> expected = PixelByPixel(g, s.outW, s.outH, s.artW, s.artH, ditherArt);
            ASSERT_EQ(baked, expected) << s.outW << "x" << s.outH << " from " << s.artW << "x" << s.artH
                                       << ", dither cell " << ditherArt;
        }
    }
}

}  // namespace

TEST(GradientBake, LinearAtEveryAngle)
{
    GradientComponent g(6.0f, 4.0f);
    SetThreeStops(g);
    for (float angle : { 0.0f, 0.4f, 1.5707963705062866f, 2.5f, 3.14159265f, -1.2f })
    {
        g.SetLinearGradient(angle);
        for (GradientDitherMode d :
             { GradientDitherMode::None, GradientDitherMode::Ordered2x2, GradientDitherMode::Ordered4x4 })
        {
            g.SetDithering(d);
            ExpectSameAsPixelByPixel(g);
        }
    }
}

TEST(GradientBake, RadialAndConical)
{
    GradientComponent g(6.0f, 4.0f);
    SetThreeStops(g);
    g.SetRadialGradient(0.3f, 0.6f, 0.7f);
    for (GradientType type : { GradientType::Radial, GradientType::Conical })
    {
        g.SetGradientType(type);
        for (GradientDitherMode d : { GradientDitherMode::None, GradientDitherMode::Ordered4x4 })
        {
            g.SetDithering(d);
            ExpectSameAsPixelByPixel(g);
        }
    }
}

TEST(GradientBake, EveryTiling)
{
    GradientComponent g(6.0f, 4.0f);
    SetThreeStops(g);
    g.SetLinearGradient(0.7f);
    const float tile = 25.0f / Deki::EngineSettings::Global().pixelsPerMeter;  // 25 art pixels
    for (GradientTileMode mode :
         { GradientTileMode::Horizontal, GradientTileMode::Vertical, GradientTileMode::Both, GradientTileMode::Mirror })
    {
        g.SetTiling(mode, tile, tile * 0.6f);
        for (GradientDitherMode d : { GradientDitherMode::None, GradientDitherMode::Ordered2x2 })
        {
            g.SetDithering(d);
            ExpectSameAsPixelByPixel(g);
        }
    }
}

// The demo's background: two stops, straight down, no dither.
TEST(GradientBake, TwoStopsStraightDown)
{
    GradientComponent g(48.0f, 24.0f);
    g.stop1Position = 0.0f;
    g.stop1Color = Deki::Color(18, 24, 48);
    g.stop2Position = 1.0f;
    g.stop2Color = Deki::Color(70, 40, 110);
    g.SetLinearGradient(1.5707963705062866f);
    g.SetDithering(GradientDitherMode::None);
    ExpectSameAsPixelByPixel(g);
}

namespace
{

struct Straight
{
    bool across, down;
};

// FindStraightAxes for `g` at 96x64, checked against the full bake: what it
// says is one colour is, and its strip holds the full bake's colours.
Straight ExpectStripsMatch(GradientComponent& g)
{
    const int32_t w = 96;
    const int32_t h = 64;
    Straight s{ false, false };
    g.FindStraightAxes(w, h, s.across, s.down);

    std::vector<uint16_t> whole(static_cast<size_t>(w) * h);
    EXPECT_TRUE(g.RenderToBuffer(reinterpret_cast<uint8_t*>(whole.data()), w, h, w, h, 1));
    if (s.across)
    {
        std::vector<uint16_t> column(h);
        EXPECT_TRUE(g.RenderToBuffer(reinterpret_cast<uint8_t*>(column.data()), 1, h, w, h, 1));
        for (int32_t y = 0; y < h; y++)
        {
            for (int32_t x = 0; x < w; x++)
            {
                EXPECT_EQ(whole[static_cast<size_t>(y) * w + x], column[y]) << "row " << y << " column " << x;
            }
        }
    }
    if (s.down)
    {
        std::vector<uint16_t> row(w);
        EXPECT_TRUE(g.RenderToBuffer(reinterpret_cast<uint8_t*>(row.data()), w, 1, w, h, 1));
        for (int32_t y = 0; y < h; y++)
        {
            for (int32_t x = 0; x < w; x++)
            {
                EXPECT_EQ(whole[static_cast<size_t>(y) * w + x], row[x]) << "row " << y << " column " << x;
            }
        }
    }
    return s;
}

}  // namespace

TEST(GradientStrip, StraightDownIsOneColumn)
{
    GradientComponent g(6.0f, 4.0f);
    SetThreeStops(g);
    g.SetDithering(GradientDitherMode::None);
    for (float angle : { 1.5707963705062866f, -1.5707963705062866f })
    {
        g.SetLinearGradient(angle);
        const Straight s = ExpectStripsMatch(g);
        EXPECT_TRUE(s.across) << "angle " << angle;
        EXPECT_FALSE(s.down) << "angle " << angle;
    }
}

TEST(GradientStrip, StraightAcrossIsOneRow)
{
    GradientComponent g(6.0f, 4.0f);
    SetThreeStops(g);
    g.SetDithering(GradientDitherMode::None);
    for (float angle : { 0.0f, 3.14159265f })
    {
        g.SetLinearGradient(angle);
        const Straight s = ExpectStripsMatch(g);
        EXPECT_TRUE(s.down) << "angle " << angle;
        EXPECT_FALSE(s.across) << "angle " << angle;
    }
}

TEST(GradientStrip, TiledAndMirroredStayStraight)
{
    GradientComponent g(6.0f, 4.0f);
    SetThreeStops(g);
    g.SetDithering(GradientDitherMode::None);
    g.SetLinearGradient(1.5707963705062866f);
    const float tile = 25.0f / Deki::EngineSettings::Global().pixelsPerMeter;
    for (GradientTileMode mode :
         { GradientTileMode::Horizontal, GradientTileMode::Vertical, GradientTileMode::Both, GradientTileMode::Mirror })
    {
        g.SetTiling(mode, tile, tile * 0.6f);
        EXPECT_TRUE(ExpectStripsMatch(g).across) << "tile mode " << static_cast<int>(mode);
    }
}

TEST(GradientStrip, EverythingElseIsBakedWhole)
{
    GradientComponent g(6.0f, 4.0f);
    SetThreeStops(g);

    // At an angle.
    g.SetDithering(GradientDitherMode::None);
    g.SetLinearGradient(0.4f);
    Straight s = ExpectStripsMatch(g);
    EXPECT_FALSE(s.across || s.down);

    // Dithered: the pattern changes along both axes.
    g.SetLinearGradient(1.5707963705062866f);
    g.SetDithering(GradientDitherMode::Ordered4x4);
    s = ExpectStripsMatch(g);
    EXPECT_FALSE(s.across || s.down);

    // Radial.
    g.SetDithering(GradientDitherMode::None);
    g.SetRadialGradient(0.5f, 0.5f, 0.5f);
    s = ExpectStripsMatch(g);
    EXPECT_FALSE(s.across || s.down);
}
