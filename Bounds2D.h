#pragma once

#include <stdint.h>

/**
 * @brief 2D axis-aligned bounding box
 *
 * Simple rectangle defined by width, height, and optional padding on each side.
 * Used by ButtonComponent and other components that need a hit area.
 */
struct Bounds2D
{
    int32_t width = 0;
    int32_t height = 0;
    int32_t padding_left = 0;
    int32_t padding_right = 0;
    int32_t padding_top = 0;
    int32_t padding_bottom = 0;

    Bounds2D() = default;

    Bounds2D(int32_t w, int32_t h)
        : width(w), height(h) {}

    /**
     * @brief Get total width including padding
     */
    int32_t GetTotalWidth() const { return padding_left + width + padding_right; }

    /**
     * @brief Get total height including padding
     */
    int32_t GetTotalHeight() const { return padding_top + height + padding_bottom; }

    /**
     * @brief Set uniform padding on all sides
     */
    void SetPadding(int32_t padding)
    {
        padding_left = padding_right = padding_top = padding_bottom = padding;
    }

    /**
     * @brief Set padding per side
     */
    void SetPadding(int32_t left, int32_t right, int32_t top, int32_t bottom)
    {
        padding_left = left;
        padding_right = right;
        padding_top = top;
        padding_bottom = bottom;
    }

    /**
     * @brief Hit test a point against the bounds
     * @param x Point X relative to the bounds origin (top-left of the content area)
     * @param y Point Y relative to the bounds origin (top-left of the content area)
     * @return true if the point is inside (including padding)
     */
    bool Contains(int32_t x, int32_t y) const
    {
        return x >= -padding_left &&
               x <= width + padding_right &&
               y >= -padding_top &&
               y <= height + padding_bottom;
    }
};
