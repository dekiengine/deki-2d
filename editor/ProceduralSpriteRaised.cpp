#include "ProceduralSpriteRaised.h"

#ifdef DEKI_EDITOR

#include <algorithm>
#include <cmath>

namespace Deki2D
{

namespace
{
float Smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// Signed distance to a rectangle with a radius per corner (negative inside),
// as the procedural sprite's own bake measures it.
float RoundedRect(float px, float py, float halfW, float halfH, const float radii[4])
{
    float r = px < 0.0f ? (py < 0.0f ? radii[0] : radii[3]) : (py < 0.0f ? radii[1] : radii[2]);
    r = std::min(r, std::min(halfW, halfH));
    const float qx = std::fabs(px) - halfW + r;
    const float qy = std::fabs(py) - halfH + r;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

void Mix(uint8_t* p, const uint8_t c[4], float t)
{
    if (t <= 0.0f)
    {
        return;
    }
    for (int i = 0; i < 3; ++i)
    {
        p[i] = static_cast<uint8_t>(std::lround(p[i] + (c[i] - p[i]) * std::min(t, 1.0f)));
    }
}
}  // namespace

void ApplyRaisedLook(uint8_t* rgba, int32_t width, int32_t height, const float radii[4], bool antialiased,
                     const RaisedLook& look)
{
    if (!rgba || width <= 0 || height <= 0 || !look.Any())
    {
        return;
    }
    const float halfW = width * 0.5f, halfH = height * 0.5f;
    const float depth = static_cast<float>(std::min(look.depth, height));
    const float outline = static_cast<float>(look.outlineWidth);
    // The face: as wide, `depth` shorter, its top on the sprite's top.
    const float faceHalfH = halfH - depth * 0.5f;
    for (int32_t y = 0; y < height; ++y)
    {
        for (int32_t x = 0; x < width; ++x)
        {
            uint8_t* p = rgba + (static_cast<size_t>(y) * width + x) * 4;
            if (p[3] == 0)
            {
                continue;  // outside the shape: nothing to colour
            }
            const float px = x + 0.5f - halfW;
            const float py = y + 0.5f - halfH;
            if (depth > 0.0f)
            {
                const float faceDist = faceHalfH > 0.0f ? RoundedRect(px, py + depth * 0.5f, halfW, faceHalfH, radii)
                                                        : 1.0f;
                Mix(p, look.depthColor, antialiased ? Smoothstep(-0.5f, 0.5f, faceDist) : (faceDist > 0.0f ? 1.0f : 0.0f));
            }
            if (outline > 0.0f)
            {
                const float outerDist = RoundedRect(px, py, halfW, halfH, radii);
                Mix(p, look.outlineColor,
                    antialiased ? Smoothstep(-0.5f, 0.5f, outerDist + outline) : (outerDist > -outline ? 1.0f : 0.0f));
            }
        }
    }
}

}  // namespace Deki2D

#endif  // DEKI_EDITOR
