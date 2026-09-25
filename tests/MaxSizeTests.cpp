// Max Size: a texture stored smaller than its image. The encoder resamples
// each frame into the rect its edges map to, and the loader maps the file's
// frames (in the image's pixels) with the same rule; these pin both halves.

#include <gtest/gtest.h>

#include <Sprite.h>
#include <editor/ImageResample.h>

#include <cstdint>
#include <vector>

using namespace Deki2D;
using namespace Deki2DEditor;

namespace
{
std::vector<uint8_t> Solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4)
    {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}
}  // namespace

// The encoder's edge rule and the runtime's are the same numbers.
TEST(MaxSize, EdgeRuleMatchesTheRuntime)
{
    for (int src : { 7, 72, 100, 3000 })
        for (int stored : { 1, 5, 32, 64, 2048 })
            for (int v = 0; v <= src; v += (src / 7) + 1)
                EXPECT_EQ(MapEdge(v, src, stored), Sprite::SourceToStored(v, src, stored)) << v << " " << src << " " << stored;
}

// Two frames side by side, red and blue: shrunk to an odd size, no stored
// pixel mixes them, because each frame is resampled on its own.
TEST(MaxSize, FramesDoNotBleed)
{
    const int w = 48, h = 24;
    std::vector<uint8_t> src = Solid(w, h, 255, 0, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 24; x < 48; ++x)
        {
            uint8_t* p = &src[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = 0;
            p[2] = 255;
        }
    const int dw = 21, dh = 11;  // not a clean fraction
    const std::vector<uint8_t> dst =
        ShrinkImage(src.data(), w, h, dw, dh, { { 0, 0, 24, 24 }, { 24, 0, 48, 24 } }, false);
    const int edge = MapEdge(24, w, dw);
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x)
        {
            const uint8_t* p = &dst[(static_cast<size_t>(y) * dw + x) * 4];
            if (x < edge)
                EXPECT_TRUE(p[0] == 255 && p[2] == 0) << x << "," << y;
            else
                EXPECT_TRUE(p[0] == 0 && p[2] == 255) << x << "," << y;
        }
}

// Averaging weights colour by alpha: a transparent black pixel beside a white
// one does not grey the result.
TEST(MaxSize, TransparentPixelsDoNotDarken)
{
    std::vector<uint8_t> src = { 255, 255, 255, 255, 0, 0, 0, 0 };  // 2x1: white, transparent black
    std::vector<uint8_t> dst(4);
    ResampleRect(src.data(), 2, { 0, 0, 2, 1 }, dst.data(), 1, { 0, 0, 1, 1 }, false);
    EXPECT_EQ(dst[0], 255);
    EXPECT_EQ(dst[3], 128);
}

// Chroma-keyed images keep exact colours: nearest, not a blend.
TEST(MaxSize, NearestKeepsExactColours)
{
    std::vector<uint8_t> src = Solid(4, 4, 255, 0, 255);
    src[0] = 10;  // a pixel that is not the key
    std::vector<uint8_t> dst = ShrinkImage(src.data(), 4, 4, 2, 2, {}, true);
    for (size_t i = 0; i < dst.size(); i += 4)
        EXPECT_TRUE((dst[i] == 255 || dst[i] == 10) && dst[i + 1] == 0 && dst[i + 2] == 255);
}

// The loader brings frames, the default frame, nine-slice borders and
// pixels-per-meter to the stored pixels, so the sprite keeps its world size.
TEST(MaxSize, LoaderMapsToStoredPixels)
{
    Sprite s;
    s.width = 36;
    s.height = 12;  // stored: half of 72 x 24
    SpriteFrame f{};
    f.x = 24;
    f.y = 0;
    f.width = 24;
    f.height = 24;
    s.frames.push_back(f);
    s.defaultFrameWidth = 24;
    s.defaultFrameHeight = 24;
    s.hasNineSlice = true;
    s.nineSliceLeft = 4;
    s.nineSliceRight = 6;
    s.nineSliceTop = 2;
    s.nineSliceBottom = 8;
    const float ppm = s.pixelsPerMeter;

    s.ApplySourceSize(72, 24);

    EXPECT_EQ(s.sourceWidth, 72);
    EXPECT_FLOAT_EQ(s.sourceScale, 0.5f);
    EXPECT_FLOAT_EQ(s.pixelsPerMeter, ppm * 0.5f);
    EXPECT_EQ(s.frames[0].x, 12);
    EXPECT_EQ(s.frames[0].width, 12);
    EXPECT_EQ(s.frames[0].height, 12);
    EXPECT_EQ(s.defaultFrameWidth, 12);
    EXPECT_EQ(s.nineSliceLeft, 2);
    EXPECT_EQ(s.nineSliceRight, 3);
    EXPECT_EQ(s.nineSliceTop, 1);
    EXPECT_EQ(s.nineSliceBottom, 4);
    // Same world size: 24 image pixels at ppm, 12 stored at ppm / 2.
    EXPECT_FLOAT_EQ(s.frames[0].width / s.pixelsPerMeter, 24.0f / ppm);
}

// A texture that was not shrunk is left alone.
TEST(MaxSize, UnshrunkIsUnchanged)
{
    Sprite s;
    s.width = 72;
    s.height = 24;
    s.defaultFrameWidth = 24;
    const float ppm = s.pixelsPerMeter;
    s.ApplySourceSize(72, 24);
    s.ApplySourceSize(0, 0);
    EXPECT_EQ(s.sourceWidth, 0);
    EXPECT_EQ(s.defaultFrameWidth, 24);
    EXPECT_FLOAT_EQ(s.pixelsPerMeter, ppm);
    EXPECT_FLOAT_EQ(s.sourceScale, 1.0f);
}
