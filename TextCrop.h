#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace Deki2D
{

// The pivot that keeps a crop of a text bake where the whole bake was. The
// whole one pivots on its centre, and Blit puts a source floor(size * pivot)
// pixels before its anchor, so the crop's pivot must give
// floor(crop * pivot) == floor(full / 2) - first.
//
// Divided out, it can land just under that whole number, so it is nudged up
// until it does not. Up means towards infinity: a short line at the start of
// a wide box needs a pivot above 2, and a nudge towards 2 took it down for
// ever, which hung the game on that text.
//
// In its own header so the rule can be tested directly.
inline float CropPivot(int32_t full, int32_t first, int32_t crop)
{
    const int32_t want = full / 2 - first;
    float pivot = (0.5f * static_cast<float>(full) - static_cast<float>(first)) / static_cast<float>(crop);
    while (static_cast<int32_t>(std::floor(static_cast<float>(crop) * pivot)) < want)
    {
        pivot = std::nextafter(pivot, std::numeric_limits<float>::infinity());
    }
    return pivot;
}

}  // namespace Deki2D
