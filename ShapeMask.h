#pragma once

#include <cstdint>

namespace Deki2D
{

/// A full turn in radians.
inline constexpr float kTwoPi = 6.2831853f;

/// What a ShapeComponent draws.
enum class ShapeKind : uint8_t
{
    Rectangle = 0,  // With rounded corners when cornerRadius is above 0
    Ellipse = 1,    // A circle when width and height match
    Arc = 2,        // Part of a ring, from startAngle to endAngle
    Triangle = 3,   // Pointing the way of `direction`, like a dropdown arrow
    Check = 4,      // A tick mark
    Custom = 5      // Pixels written by code (ShapeComponent::BeginCustomMask)
};

/// Which way a Triangle points.
enum class ShapeDirection : uint8_t
{
    Down = 0,
    Up = 1,
    Left = 2,
    Right = 3
};

/// Everything a shape's coverage depends on, in pixels and radians.
struct ShapeMaskParams
{
    ShapeKind kind = ShapeKind::Rectangle;
    float width = 0.0f;
    float height = 0.0f;
    float cornerRadius = 0.0f;
    // 0 fills the shape; above 0 draws only a line this thick along the
    // inside of its edge. An Arc and a Check are always lines; 0 picks a
    // thickness from their size.
    float lineWidth = 0.0f;
    // Arc only. 0 points right, angles grow counter-clockwise, and the arc
    // runs counter-clockwise from startAngle to endAngle.
    float startAngle = 0.0f;
    float endAngle = 6.2831853f;
    bool roundCaps = false;
    ShapeDirection direction = ShapeDirection::Down;
};

/// The line thickness an Arc or Check uses when its lineWidth is 0.
float DefaultShapeLineWidth(const ShapeMaskParams& params);

/// Writes the shape's coverage into an 8-bit mask `w` x `h` pixels, row 0 at
/// the top, `stride` bytes per row: 255 inside, 0 outside, and in between on
/// the one-pixel edge, so the edges are smooth at any size. The shape fills
/// the mask; params.width and params.height say how many pixels that is (the
/// mask's own size, normally). Custom writes nothing. Returns false when the
/// size is not positive.
bool BakeShapeMask(uint8_t* mask, int32_t w, int32_t h, int32_t stride, const ShapeMaskParams& params);

/// One pixel's coverage, 0 to 1, at (x, y) from the shape's centre in
/// pixels, y up. What BakeShapeMask writes for each pixel centre.
float ShapeCoverage(const ShapeMaskParams& params, float x, float y);

}  // namespace Deki2D
