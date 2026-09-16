#pragma once

#include <cstdint>

namespace Deki2D
{

// Per-row opaque spans for RGB565A8 pixels: [start, end) of the LONGEST run of
// fully opaque pixels in each row.
//
// The blitter copies that run straight and blends everything outside it per
// pixel, so the run must not contain a soft or transparent pixel. Recording
// first-to-last opaque instead is what made the notch of a heart and the gap
// between two legs come out in whatever colour the transparent pixel carried.
//
// A row with no opaque pixel gets an EMPTY span at the row end (start = end =
// w), so the blitter's left region covers the row once and its right region is
// empty. start=w, end=0 made both regions cover the whole row: every soft
// pixel blended twice, and the right loop started at x=0 regardless of the
// clip rect.
//
// In its own header so the rule can be tested directly. It is the kind of
// small index calculation whose failure is visible only as a wrong pixel in a
// finished frame, and it has been wrong before.
//
// `pixel_data` is w*h*3 bytes, alpha at byte 2 of each pixel. `spans` receives
// 2 int16 per row: [start, end).
inline void BuildOpaqueRowSpans(const uint8_t* pixel_data, int32_t w, int32_t h, int16_t* spans)
{
    for (int32_t y = 0; y < h; y++)
    {
        const uint8_t* row = pixel_data + y * w * 3;
        int32_t bestStart = w, bestEnd = w;
        int32_t runStart = -1;
        for (int32_t x = 0; x <= w; x++)
        {
            const bool opaque = (x < w) && row[x * 3 + 2] == 255;
            if (opaque && runStart < 0)
                runStart = x;
            if (!opaque && runStart >= 0)
            {
                if (x - runStart > bestEnd - bestStart || bestStart >= w)
                {
                    bestStart = runStart;
                    bestEnd = x;
                }
                runStart = -1;
            }
        }
        spans[y * 2] = (int16_t)bestStart;
        spans[y * 2 + 1] = (int16_t)bestEnd;
    }
}

}  // namespace Deki2D
