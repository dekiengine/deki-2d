#include "ShapeMask.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Deki2D
{

// Each shape is a signed distance: how far a point is outside the shape's
// edge, in pixels, negative inside. Coverage is that distance turned into a
// one-pixel ramp across the edge, which is what makes the edges smooth.

namespace
{

float Length(float x, float y)
{
    return std::sqrt(x * x + y * y);
}

float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Distance from (px, py) to the segment a-b.
float SegmentDistance(float px, float py, float ax, float ay, float bx, float by)
{
    const float pax = px - ax;
    const float pay = py - ay;
    const float bax = bx - ax;
    const float bay = by - ay;
    const float lengthSquared = bax * bax + bay * bay;
    const float t = lengthSquared > 0.0f ? Clamp01((pax * bax + pay * bay) / lengthSquared) : 0.0f;
    return Length(pax - bax * t, pay - bay * t);
}

float RectangleDistance(float x, float y, float halfW, float halfH, float radius)
{
    const float qx = std::fabs(x) - (halfW - radius);
    const float qy = std::fabs(y) - (halfH - radius);
    const float outside = Length(std::max(qx, 0.0f), std::max(qy, 0.0f));
    const float inside = std::min(std::max(qx, qy), 0.0f);
    return outside + inside - radius;
}

float EllipseDistance(float x, float y, float halfW, float halfH)
{
    if (std::fabs(halfW - halfH) < 0.001f)
    {
        return Length(x, y) - halfW;
    }
    // A close estimate rather than the exact distance, which needs a cubic
    // solve: the ratio of the point's distance in the ellipse's own scale to
    // the gradient of that scale. Exact on the edge, which is what the
    // coverage ramp reads.
    const float k0 = Length(x / halfW, y / halfH);
    const float k1 = Length(x / (halfW * halfW), y / (halfH * halfH));
    if (k1 <= 0.0f)
    {
        return -std::min(halfW, halfH);
    }
    return k0 * (k0 - 1.0f) / k1;
}

// Distance to the triangle a-b-c, negative inside.
float TriangleDistance(float px, float py, const float* v)
{
    float best = 1e30f;
    float sign = 1.0f;
    bool inside = true;
    for (int i = 0; i < 3; ++i)
    {
        const float ax = v[i * 2];
        const float ay = v[i * 2 + 1];
        const float bx = v[((i + 1) % 3) * 2];
        const float by = v[((i + 1) % 3) * 2 + 1];
        best = std::min(best, SegmentDistance(px, py, ax, ay, bx, by));
        // The vertices run counter-clockwise, so inside is left of every edge.
        if ((bx - ax) * (py - ay) - (by - ay) * (px - ax) < 0.0f)
        {
            inside = false;
        }
    }
    if (inside)
    {
        sign = -1.0f;
    }
    return best * sign;
}

// Distance from a point to a ray from the origin along (ux, uy), a unit vector.
float RayDistance(float x, float y, float ux, float uy)
{
    const float t = x * ux + y * uy;
    if (t <= 0.0f)
    {
        return Length(x, y);
    }
    return std::fabs(x * uy - y * ux);
}

float ArcDistance(const ShapeMaskParams& p, float x, float y, float lineWidth)
{
    const float halfLine = lineWidth * 0.5f;
    const float radius = std::max(0.0f, std::min(p.width, p.height) * 0.5f - halfLine);
    const float ring = std::fabs(Length(x, y) - radius) - halfLine;

    float sweep = p.endAngle - p.startAngle;
    if (sweep >= kTwoPi - 1e-4f || sweep <= -kTwoPi + 1e-4f)
    {
        return ring;
    }
    sweep = std::fmod(sweep, kTwoPi);
    if (sweep < 0.0f)
    {
        sweep += kTwoPi;
    }
    if (sweep <= 0.0f)
    {
        return 1e30f;
    }

    const float u0x = std::cos(p.startAngle);
    const float u0y = std::sin(p.startAngle);
    const float u1x = std::cos(p.startAngle + sweep);
    const float u1y = std::sin(p.startAngle + sweep);

    // Inside the angle range, worked out with cross products instead of
    // atan2 on every pixel. A sweep over half a turn is the complement of a
    // wedge under half a turn.
    const float c0 = u0x * y - u0y * x;  // > 0: counter-clockwise of the start
    const float c1 = x * u1y - y * u1x;  // > 0: clockwise of the end
    const bool within = sweep <= kTwoPi * 0.5f ? (c0 >= 0.0f && c1 >= 0.0f) : !(c0 < 0.0f && c1 < 0.0f);

    if (p.roundCaps)
    {
        if (within)
        {
            return ring;
        }
        const float cap0 = Length(x - u0x * radius, y - u0y * radius);
        const float cap1 = Length(x - u1x * radius, y - u1y * radius);
        return std::min(cap0, cap1) - halfLine;
    }

    const float toRays = std::min(RayDistance(x, y, u0x, u0y), RayDistance(x, y, u1x, u1y));
    return std::max(ring, within ? -toRays : toRays);
}

float CheckDistance(const ShapeMaskParams& p, float x, float y, float lineWidth)
{
    // The tick in a unit box, scaled evenly so it keeps its shape.
    const float s = std::min(p.width, p.height);
    const float ax = -0.36f * s;
    const float ay = 0.02f * s;
    const float bx = -0.12f * s;
    const float by = -0.24f * s;
    const float cx = 0.36f * s;
    const float cy = 0.26f * s;
    const float d = std::min(SegmentDistance(x, y, ax, ay, bx, by), SegmentDistance(x, y, bx, by, cx, cy));
    return d - lineWidth * 0.5f;
}

float ShapeDistance(const ShapeMaskParams& p, float x, float y)
{
    const float halfW = p.width * 0.5f;
    const float halfH = p.height * 0.5f;
    float d = 1e30f;
    switch (p.kind)
    {
        case ShapeKind::Rectangle:
        {
            const float radius = std::max(0.0f, std::min(p.cornerRadius, std::min(halfW, halfH)));
            d = RectangleDistance(x, y, halfW, halfH, radius);
            break;
        }
        case ShapeKind::Ellipse: d = EllipseDistance(x, y, halfW, halfH); break;
        case ShapeKind::Triangle:
        {
            // Counter-clockwise, pointing down; the others are this turned.
            float v[6] = { 0.0f, -halfH, halfW, halfH, -halfW, halfH };
            if (p.direction == ShapeDirection::Up)
            {
                const float up[6] = { 0.0f, halfH, -halfW, -halfH, halfW, -halfH };
                std::memcpy(v, up, sizeof(v));
            }
            else if (p.direction == ShapeDirection::Left)
            {
                const float left[6] = { -halfW, 0.0f, halfW, -halfH, halfW, halfH };
                std::memcpy(v, left, sizeof(v));
            }
            else if (p.direction == ShapeDirection::Right)
            {
                const float right[6] = { halfW, 0.0f, -halfW, halfH, -halfW, -halfH };
                std::memcpy(v, right, sizeof(v));
            }
            d = TriangleDistance(x, y, v);
            break;
        }
        case ShapeKind::Arc:
        {
            const float line = p.lineWidth > 0.0f ? p.lineWidth : DefaultShapeLineWidth(p);
            return ArcDistance(p, x, y, line);
        }
        case ShapeKind::Check:
        {
            const float line = p.lineWidth > 0.0f ? p.lineWidth : DefaultShapeLineWidth(p);
            return CheckDistance(p, x, y, line);
        }
        case ShapeKind::Custom: return 1e30f;
    }

    // An outline: the band lineWidth deep inside the edge.
    if (p.lineWidth > 0.0f)
    {
        const float half = p.lineWidth * 0.5f;
        d = std::fabs(d + half) - half;
    }
    return d;
}

}  // namespace

float DefaultShapeLineWidth(const ShapeMaskParams& params)
{
    const float scale = params.kind == ShapeKind::Check ? 0.14f : 0.12f;
    return std::max(1.0f, std::min(params.width, params.height) * scale);
}

float ShapeCoverage(const ShapeMaskParams& params, float x, float y)
{
    return Clamp01(0.5f - ShapeDistance(params, x, y));
}

bool BakeShapeMask(uint8_t* mask, int32_t w, int32_t h, int32_t stride, const ShapeMaskParams& params)
{
    if (!mask || w <= 0 || h <= 0 || stride < w)
    {
        return false;
    }
    if (params.kind == ShapeKind::Custom)
    {
        return true;
    }

    // Rectangles and ellipses are the same mirrored left to right and top
    // to bottom, so a quarter is worked out and copied to the rest.
    const bool mirrored = params.kind == ShapeKind::Rectangle || params.kind == ShapeKind::Ellipse;
    const int32_t cols = mirrored ? (w + 1) / 2 : w;
    const int32_t rows = mirrored ? (h + 1) / 2 : h;
    const float halfW = static_cast<float>(w) * 0.5f;
    const float halfH = static_cast<float>(h) * 0.5f;

    for (int32_t j = 0; j < rows; ++j)
    {
        uint8_t* row = mask + static_cast<size_t>(j) * stride;
        const float y = halfH - (static_cast<float>(j) + 0.5f);
        for (int32_t i = 0; i < cols; ++i)
        {
            const float x = (static_cast<float>(i) + 0.5f) - halfW;
            row[i] = static_cast<uint8_t>(ShapeCoverage(params, x, y) * 255.0f + 0.5f);
        }
        if (mirrored)
        {
            for (int32_t i = cols; i < w; ++i)
            {
                row[i] = row[w - 1 - i];
            }
        }
    }
    if (mirrored)
    {
        for (int32_t j = rows; j < h; ++j)
        {
            std::memcpy(mask + static_cast<size_t>(j) * stride, mask + static_cast<size_t>(h - 1 - j) * stride, w);
        }
    }
    return true;
}

}  // namespace Deki2D
