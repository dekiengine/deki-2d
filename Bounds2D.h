#pragma once

#include <stdint.h>

namespace Deki2D
{

/// A 2D axis-aligned rectangle: a width and height plus optional padding on
/// each side. All values are in world meters, like the rest of the engine.
/// Components that need a hit area use it; editor gizmos multiply by the
/// camera's pixelsPerMeter to get screen pixels.
struct Bounds2D
{
    float width = 0.0f;
    float height = 0.0f;
    float paddingLeft = 0.0f;
    float paddingRight = 0.0f;
    float paddingTop = 0.0f;
    float paddingBottom = 0.0f;

    Bounds2D() = default;

    Bounds2D(float w, float h)
        : width(w),
          height(h)
    {
    }

    /// Width including padding.
    float GetTotalWidth() const { return paddingLeft + width + paddingRight; }

    /// Height including padding.
    float GetTotalHeight() const { return paddingTop + height + paddingBottom; }

    /// Sets the same padding on all sides.
    void SetPadding(float padding) { paddingLeft = paddingRight = paddingTop = paddingBottom = padding; }

    void SetPadding(float left, float right, float top, float bottom)
    {
        paddingLeft = left;
        paddingRight = right;
        paddingTop = top;
        paddingBottom = bottom;
    }

    /// True when the point is inside the bounds, padding included. `x` and `y`
    /// are relative to the top-left of the content area.
    bool Contains(float x, float y) const
    {
        return x >= -paddingLeft && x <= width + paddingRight && y >= -paddingTop && y <= height + paddingBottom;
    }
};

}  // namespace Deki2D
