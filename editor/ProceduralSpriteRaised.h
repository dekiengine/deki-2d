#pragma once

// The "raised" look of a procedural sprite: a darker band along its bottom,
// under a face the band's height shorter, and an outline all round, as a key
// or a game's HUD panel is drawn. Applied over a baked procedural sprite; the
// editor bakes, a game draws the result as one sprite (in NineSlice mode when
// it is stretched).

#include <cstdint>

namespace Deki2D
{

struct RaisedLook
{
    int32_t outlineWidth = 0;            // pixels, on the outer edge; 0 = none
    uint8_t outlineColor[4] = { 0, 0, 0, 255 };
    int32_t depth = 0;                   // the band's height in pixels; 0 = none
    uint8_t depthColor[4] = { 0, 0, 0, 255 };

    bool Any() const { return outlineWidth > 0 || depth > 0; }
};

/// Recolours `rgba` (`width` x `height`, 4 bytes a pixel, straight alpha), a
/// rounded rectangle baked with corner radii `radii` (top left, top right,
/// bottom right, bottom left): inside it, below a face as wide and `depth`
/// shorter, aligned to the top, the depth colour; within `outlineWidth` of its
/// edge, the outline colour. Alpha is left as baked. Antialiased edges blend
/// as the bake's do. Does nothing when `look` has neither.
void ApplyRaisedLook(uint8_t* rgba, int32_t width, int32_t height, const float radii[4], bool antialiased,
                     const RaisedLook& look);

}  // namespace Deki2D
