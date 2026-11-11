// The raised look of a procedural sprite (ProceduralSpriteRaised.h): a band
// along the bottom under a face that much shorter, and an outline all round,
// as the guide's HUD panels drew with three stacked shapes.

#include <gtest/gtest.h>

#include "../editor/ProceduralSpriteRaised.h"

#include <cmath>
#include <vector>

using namespace Deki2D;

namespace
{
constexpr int kW = 60, kH = 28;
const float kRadii[4] = { 6, 6, 6, 6 };
const uint8_t kFace[4] = { 255, 239, 208, 255 };

// A 60 x 28 panel in its face colour, corners cut round, as a procedural
// sprite bakes it without antialiasing.
std::vector<uint8_t> Panel()
{
    std::vector<uint8_t> rgba(kW * kH * 4, 0);
    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            const float px = x + 0.5f - kW * 0.5f, py = y + 0.5f - kH * 0.5f;
            const float qx = std::fabs(px) - kW * 0.5f + 6, qy = std::fabs(py) - kH * 0.5f + 6;
            const float ox = std::fmax(qx, 0.0f), oy = std::fmax(qy, 0.0f);
            const float d = std::sqrt(ox * ox + oy * oy) + std::fmin(std::fmax(qx, qy), 0.0f) - 6;
            if (d <= 0.0f)
            {
                uint8_t* p = &rgba[(y * kW + x) * 4];
                for (int i = 0; i < 4; ++i)
                {
                    p[i] = kFace[i];
                }
            }
        }
    }
    return rgba;
}

const uint8_t* At(const std::vector<uint8_t>& rgba, int x, int y)
{
    return &rgba[(y * kW + x) * 4];
}

bool Is(const uint8_t* p, uint8_t r, uint8_t g, uint8_t b)
{
    return p[0] == r && p[1] == g && p[2] == b;
}
}  // namespace

TEST(ProceduralSpriteRaised, WithoutItTheBakeIsUntouched)
{
    std::vector<uint8_t> rgba = Panel();
    const std::vector<uint8_t> before = rgba;
    ApplyRaisedLook(rgba.data(), kW, kH, kRadii, false, RaisedLook{});
    EXPECT_EQ(rgba, before);
}

TEST(ProceduralSpriteRaised, ABandUnderAShorterFaceAndAnOutlineAllRound)
{
    std::vector<uint8_t> rgba = Panel();
    const std::vector<uint8_t> before = rgba;
    RaisedLook look;
    look.outlineWidth = 1;
    look.outlineColor[0] = 107, look.outlineColor[1] = 64, look.outlineColor[2] = 36;
    look.depth = 4;
    look.depthColor[0] = 226, look.depthColor[1] = 198, look.depthColor[2] = 156;
    ApplyRaisedLook(rgba.data(), kW, kH, kRadii, false, look);

    EXPECT_TRUE(Is(At(rgba, 30, 10), 255, 239, 208)) << "the face";
    EXPECT_TRUE(Is(At(rgba, 30, 23), 255, 239, 208)) << "the face's last row: 24 rows, from the top";
    EXPECT_TRUE(Is(At(rgba, 30, 24), 226, 198, 156)) << "the band's first row";
    EXPECT_TRUE(Is(At(rgba, 30, 26), 226, 198, 156)) << "the band above the outline";
    EXPECT_TRUE(Is(At(rgba, 30, 27), 107, 64, 36)) << "the outline under the band";
    EXPECT_TRUE(Is(At(rgba, 30, 0), 107, 64, 36)) << "the outline along the top";
    EXPECT_TRUE(Is(At(rgba, 0, 14), 107, 64, 36)) << "the outline down the left";
    EXPECT_TRUE(Is(At(rgba, 59, 14), 107, 64, 36)) << "the outline down the right";
    EXPECT_TRUE(Is(At(rgba, 1, 14), 255, 239, 208)) << "the face inside the outline";
    // The face's bottom corners are round too: the band shows beside them.
    EXPECT_TRUE(Is(At(rgba, 1, 23), 226, 198, 156)) << "the band beside the face's round corner";
    // Alpha as baked, the corners still cut.
    for (size_t i = 3; i < rgba.size(); i += 4)
    {
        ASSERT_EQ(rgba[i], before[i]) << "alpha of pixel " << i / 4;
    }
    EXPECT_EQ(At(rgba, 0, 0)[3], 0);
}

TEST(ProceduralSpriteRaised, AntialiasedTheEdgesBlendAndTheMiddlesAreSolid)
{
    std::vector<uint8_t> rgba = Panel();
    RaisedLook look;
    look.outlineWidth = 1;
    look.depth = 4;
    look.depthColor[0] = 200, look.depthColor[1] = 100, look.depthColor[2] = 50;
    ApplyRaisedLook(rgba.data(), kW, kH, kRadii, true, look);
    EXPECT_TRUE(Is(At(rgba, 30, 10), 255, 239, 208)) << "the face";
    EXPECT_TRUE(Is(At(rgba, 30, 25), 200, 100, 50)) << "the band";
    const uint8_t* between = At(rgba, 30, 0);
    EXPECT_LT(between[0], 255) << "the outline darkens the top row";
}
