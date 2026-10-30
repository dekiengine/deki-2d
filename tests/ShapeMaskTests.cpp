// What a ShapeComponent's coverage mask holds: solid inside, clear outside,
// a smooth edge between, and only the angles an arc covers. The UI widgets
// draw every track, knob and ring with it, so a wrong pixel here is a wrong
// pixel on every slider.
//
// And that the component bakes only when the geometry changes, into memory
// it keeps: a bar's fill changes size every frame of a drag.

#include <gtest/gtest.h>

#include "ShapeComponent.h"
#include "ShapeMask.h"

#include <deki/Engine.h>
#include <deki/Object.h>

#include <cmath>
#include <vector>

using namespace Deki2D;

namespace
{

constexpr float kPi = 3.14159265f;

std::vector<uint8_t> Bake(const ShapeMaskParams& p, int32_t w, int32_t h)
{
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 7);
    EXPECT_TRUE(BakeShapeMask(mask.data(), w, h, w, p));
    return mask;
}

ShapeMaskParams Params(ShapeKind kind, float w, float h)
{
    ShapeMaskParams p;
    p.kind = kind;
    p.width = w;
    p.height = h;
    return p;
}

uint8_t At(const std::vector<uint8_t>& mask, int32_t w, int32_t x, int32_t y)
{
    return mask[static_cast<size_t>(y) * w + x];
}

}  // namespace

TEST(ShapeMask, SquareCornersFillEveryPixel)
{
    const auto mask = Bake(Params(ShapeKind::Rectangle, 20, 10), 20, 10);
    for (uint8_t v : mask)
    {
        EXPECT_EQ(v, 255);
    }
}

TEST(ShapeMask, RoundedCornersAreClearAndTheMiddleSolid)
{
    ShapeMaskParams p = Params(ShapeKind::Rectangle, 20, 20);
    p.cornerRadius = 8.0f;
    const auto mask = Bake(p, 20, 20);
    EXPECT_EQ(At(mask, 20, 0, 0), 0);
    EXPECT_EQ(At(mask, 20, 19, 0), 0);
    EXPECT_EQ(At(mask, 20, 0, 19), 0);
    EXPECT_EQ(At(mask, 20, 19, 19), 0);
    EXPECT_EQ(At(mask, 20, 10, 10), 255);
    // Straight edges between the corners stay solid.
    EXPECT_EQ(At(mask, 20, 10, 0), 255);
    EXPECT_EQ(At(mask, 20, 0, 10), 255);
}

TEST(ShapeMask, CircleCoversItsArea)
{
    const int32_t size = 40;
    const auto mask = Bake(Params(ShapeKind::Ellipse, size, size), size, size);
    double covered = 0.0;
    for (uint8_t v : mask)
    {
        covered += v / 255.0;
    }
    const double expected = kPi * 20.0 * 20.0;
    EXPECT_NEAR(covered, expected, expected * 0.01);
}

TEST(ShapeMask, EdgesAreSmooth)
{
    // A circle's edge pixels sit between solid and clear.
    const auto mask = Bake(Params(ShapeKind::Ellipse, 31, 31), 31, 31);
    int partial = 0;
    for (uint8_t v : mask)
    {
        if (v > 0 && v < 255)
        {
            ++partial;
        }
    }
    EXPECT_GT(partial, 40);
}

TEST(ShapeMask, MirroredHalvesMatchOnOddSizes)
{
    // Rectangles and ellipses bake a quarter and mirror it; an odd size has a
    // middle row and column that must not be copied twice.
    const int32_t w = 9;
    const int32_t h = 7;
    ShapeMaskParams p = Params(ShapeKind::Ellipse, w, h);
    const auto mask = Bake(p, w, h);
    const float halfW = w * 0.5f;
    const float halfH = h * 0.5f;
    for (int32_t y = 0; y < h; ++y)
    {
        for (int32_t x = 0; x < w; ++x)
        {
            const float cover = ShapeCoverage(p, (x + 0.5f) - halfW, halfH - (y + 0.5f));
            EXPECT_EQ(At(mask, w, x, y), static_cast<uint8_t>(cover * 255.0f + 0.5f)) << x << "," << y;
        }
    }
}

TEST(ShapeMask, OutlineHasAClearMiddle)
{
    ShapeMaskParams p = Params(ShapeKind::Rectangle, 20, 20);
    p.lineWidth = 2.0f;
    const auto mask = Bake(p, 20, 20);
    EXPECT_EQ(At(mask, 20, 10, 10), 0);
    EXPECT_EQ(At(mask, 20, 0, 10), 255);
    EXPECT_EQ(At(mask, 20, 1, 10), 255);
    EXPECT_EQ(At(mask, 20, 3, 10), 0);
}

TEST(ShapeMask, ArcCoversOnlyItsAngles)
{
    // A quarter from 0 (right) counter-clockwise to up: the top-right.
    ShapeMaskParams p = Params(ShapeKind::Arc, 40, 40);
    p.lineWidth = 4.0f;
    p.startAngle = 0.0f;
    p.endAngle = kPi * 0.5f;
    const auto mask = Bake(p, 40, 40);
    // On the ring's middle (radius 18) at 45 degrees (top-right) and at 225
    // degrees (bottom-left): pixel 33 and 7 are 12.5 and 13.5 from the centre.
    const int32_t d = 13;
    EXPECT_GT(At(mask, 40, 20 + d, 20 - d), 200);
    EXPECT_EQ(At(mask, 40, 20 - d, 20 + d), 0);
    // The middle of the ring is clear.
    EXPECT_EQ(At(mask, 40, 20, 20), 0);
}

TEST(ShapeMask, ArcPastHalfATurnLeavesOnlyTheGap)
{
    // Three quarters, from right round to down: everything but bottom-right.
    ShapeMaskParams p = Params(ShapeKind::Arc, 40, 40);
    p.lineWidth = 4.0f;
    p.startAngle = 0.0f;
    p.endAngle = kPi * 1.5f;
    const auto mask = Bake(p, 40, 40);
    const int32_t d = 13;                          // on the ring's middle, as above
    EXPECT_GT(At(mask, 40, 20 + d, 20 - d), 200);  // top-right
    EXPECT_GT(At(mask, 40, 20 - d, 20 - d), 200);  // top-left
    EXPECT_GT(At(mask, 40, 20 - d, 20 + d), 200);  // bottom-left
    EXPECT_EQ(At(mask, 40, 20 + d, 20 + d), 0);    // bottom-right
}

TEST(ShapeMask, RoundCapsReachPastTheEnds)
{
    ShapeMaskParams p = Params(ShapeKind::Arc, 40, 40);
    p.lineWidth = 6.0f;
    p.startAngle = 0.0f;
    p.endAngle = kPi * 0.5f;
    // Just below the start (right, on the ring), past the square end.
    const float x = 17.0f;
    const float y = -2.0f;
    EXPECT_LT(ShapeCoverage(p, x, y), 0.01f);
    p.roundCaps = true;
    EXPECT_GT(ShapeCoverage(p, x, y), 0.9f);
}

TEST(ShapeMask, TrianglePointsTheWayAsked)
{
    ShapeMaskParams p = Params(ShapeKind::Triangle, 20, 10);
    const auto down = Bake(p, 20, 10);
    EXPECT_GT(At(down, 20, 10, 1), 200);  // wide at the top
    EXPECT_EQ(At(down, 20, 0, 9), 0);     // narrow at the bottom
    EXPECT_EQ(At(down, 20, 19, 9), 0);

    p.direction = ShapeDirection::Up;
    const auto up = Bake(p, 20, 10);
    EXPECT_GT(At(up, 20, 10, 8), 200);
    EXPECT_EQ(At(up, 20, 0, 0), 0);
}

TEST(ShapeMask, CustomLeavesTheMaskAlone)
{
    const auto mask = Bake(Params(ShapeKind::Custom, 4, 4), 4, 4);
    for (uint8_t v : mask)
    {
        EXPECT_EQ(v, 7);
    }
}

TEST(ShapeComponent, BakesOnlyWhenTheGeometryChanges)
{
    Deki::Object obj("Shape");
    auto* shape = obj.AddComponent<ShapeComponent>();
    shape->width = 4.0f;
    shape->height = 2.0f;
    shape->cornerRadius = 1.0f;

    QuadBlit::Source src;
    float px = 0;
    float py = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    ASSERT_TRUE(shape->RenderContent(&obj, src, px, py, r, g, b, a));
    uint32_t first = 0;
    shape->GetContentVersion(first);
    const uint8_t* pixels = src.pixels;
    EXPECT_EQ(src.bytesPerPixel, 1);

    // A colour change is a tint, not a bake.
    shape->color = Deki::Color(10, 20, 30, 200);
    ASSERT_TRUE(shape->RenderContent(&obj, src, px, py, r, g, b, a));
    uint32_t second = 0;
    shape->GetContentVersion(second);
    EXPECT_EQ(first, second);
    EXPECT_EQ(r, 10);
    EXPECT_EQ(g, 20);
    EXPECT_EQ(b, 30);
    EXPECT_EQ(a, 200);

    // A smaller shape bakes again into the same memory.
    shape->width = 3.0f;
    ASSERT_TRUE(shape->RenderContent(&obj, src, px, py, r, g, b, a));
    uint32_t third = 0;
    shape->GetContentVersion(third);
    EXPECT_NE(second, third);
    EXPECT_EQ(src.pixels, pixels);
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    EXPECT_EQ(src.width, static_cast<int32_t>(std::lround(3.0f * ppm)));
}

TEST(ShapeComponent, CustomMaskDrawsWhatCodeWrote)
{
    Deki::Object obj("Shape");
    auto* shape = obj.AddComponent<ShapeComponent>();
    shape->kind = ShapeKind::Custom;
    shape->width = 2.0f;
    shape->height = 1.0f;

    QuadBlit::Source src;
    float px = 0;
    float py = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    EXPECT_FALSE(shape->RenderContent(&obj, src, px, py, r, g, b, a));

    uint8_t* mask = shape->BeginCustomMask(8, 4);
    ASSERT_NE(mask, nullptr);
    EXPECT_EQ(mask[5], 0);
    mask[5] = 255;
    shape->EndCustomMask();
    ASSERT_TRUE(shape->RenderContent(&obj, src, px, py, r, g, b, a));
    EXPECT_EQ(src.width, 8);
    EXPECT_EQ(src.height, 4);
    EXPECT_EQ(src.pixels[5], 255);
    EXPECT_FLOAT_EQ(src.pixelsPerMeter, 4.0f);
}
