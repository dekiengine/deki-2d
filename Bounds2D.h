#pragma once

#include <stdint.h>

/**
 * @brief 2D axis-aligned bounding box
 *
 * Simple rectangle defined by width, height, and optional padding on each side.
 * All values are in world meters (matches the engine's meters-internal
 * convention). Used by ButtonComponent and other components that need a hit
 * area. Editor gizmo paths multiply by the camera's pixelsPerMeter to get
 * screen pixels.
 */
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
        : width(w), height(h) {}

    /**
     * @brief Get total width including padding
     */
    float GetTotalWidth() const { return paddingLeft + width + paddingRight; }

    /**
     * @brief Get total height including padding
     */
    float GetTotalHeight() const { return paddingTop + height + paddingBottom; }

    /**
     * @brief Set uniform padding on all sides
     */
    void SetPadding(float padding)
    {
        paddingLeft = paddingRight = paddingTop = paddingBottom = padding;
    }

    /**
     * @brief Set padding per side
     */
    void SetPadding(float left, float right, float top, float bottom)
    {
        paddingLeft = left;
        paddingRight = right;
        paddingTop = top;
        paddingBottom = bottom;
    }

    /**
     * @brief Hit test a point against the bounds
     * @param x Point X relative to the bounds origin (top-left of the content area)
     * @param y Point Y relative to the bounds origin (top-left of the content area)
     * @return true if the point is inside (including padding)
     */
    bool Contains(float x, float y) const
    {
        return x >= -paddingLeft &&
               x <= width + paddingRight &&
               y >= -paddingTop &&
               y <= height + paddingBottom;
    }
};
