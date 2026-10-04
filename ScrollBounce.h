#pragma once

#include <algorithm>
#include <cmath>

// The rubber band a ScrollComponent with enableBounce shows at its ends. The
// scroll offset runs from 0 to maxScroll; past either end the list is
// "overscrolled" and springs back once nothing holds it there.
namespace Deki2D::ScrollBounce
{

// How far past an end a drag can pull the list, as a share of the viewport.
constexpr float kMaxOverscrollShare = 0.5f;

// Past an end, the list follows this share of the finger's movement.
constexpr float kDragResistance = 0.4f;

// Closer to the end than this (in meters), the spring snaps the rest of the way.
constexpr float kArriveDistance = 0.004f;

/// How far `offset` is past an end: negative before 0, positive beyond
/// `maxScroll`, 0 inside the range.
inline float Overscroll(float offset, float maxScroll)
{
    if (offset < 0.0f)
    {
        return offset;
    }
    if (offset > maxScroll)
    {
        return offset - maxScroll;
    }
    return 0.0f;
}

/// The offset after the finger moves by `delta`. Inside the range the list
/// follows the finger; the part of a move that goes past an end is slowed by
/// kDragResistance, and the list stops kMaxOverscrollShare of the viewport
/// past the end. Moving back toward the range is never slowed.
inline float Drag(float offset, float delta, float maxScroll, float viewport)
{
    float next = offset + delta;
    if (delta < 0.0f && next < 0.0f)
    {
        const float base = std::min(offset, 0.0f);
        next = base + (next - base) * kDragResistance;
    }
    else if (delta > 0.0f && next > maxScroll)
    {
        const float base = std::max(offset, maxScroll);
        next = base + (next - base) * kDragResistance;
    }
    const float limit = std::max(0.0f, viewport) * kMaxOverscrollShare;
    return std::clamp(next, -limit, maxScroll + limit);
}

/// One step of the spring that pulls an overscrolled list back to its end.
/// `stiffness` is the share of the remaining distance closed per 60 Hz frame
/// (0.15 by default), scaled to `deltaTime` so the speed does not depend on
/// the frame rate. Returns `offset` unchanged when the list is inside its range.
inline float SpringBack(float offset, float maxScroll, float stiffness, float deltaTime)
{
    const float over = Overscroll(offset, maxScroll);
    if (over == 0.0f)
    {
        return offset;
    }
    const float edge = offset - over;
    const float perFrame = std::clamp(stiffness, 0.01f, 1.0f);
    const float share = 1.0f - std::pow(1.0f - perFrame, std::max(0.0f, deltaTime) * 60.0f);
    const float next = offset + (edge - offset) * share;
    return std::fabs(next - edge) < kArriveDistance ? edge : next;
}

}  // namespace Deki2D::ScrollBounce
