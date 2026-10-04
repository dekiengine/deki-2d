#pragma once

#include <cstdint>

namespace Deki2D
{

// Per-row opaque spans for RGB565A8 pixels: [start, end) of the LONGEST run of
// fully opaque pixels in each row.
//
// The blitter copies that run straight and blends everything outside it per
// pixel, so the run must not contain a soft or transparent pixel. It must not
// be "first to last opaque pixel": then the notch of a heart or the gap
// between two legs comes out in whatever colour the transparent pixel holds.
//
// A row with no opaque pixel gets an EMPTY span at the row end (start = end =
// w), so the blitter's left region covers the row once and its right region is
// empty. It must not be start=w, end=0: then both regions cover the whole row,
// every soft pixel blends twice, and the right loop ignores the clip rect.
//
// In its own header so the rule can be tested directly: a mistake here shows
// only as a wrong pixel in a finished frame.
//
// `pixelData` is w*h*3 bytes, alpha at byte 2 of each pixel. `spans` receives
// 2 int16 per row: [start, end).
inline void BuildOpaqueRowSpans(const uint8_t* pixelData, int32_t w, int32_t h, int16_t* spans)
{
    for (int32_t y = 0; y < h; y++)
    {
        const uint8_t* row = pixelData + y * w * 3;
        int32_t bestStart = w, bestEnd = w;
        int32_t runStart = -1;
        for (int32_t x = 0; x <= w; x++)
        {
            const bool opaque = (x < w) && row[x * 3 + 2] == 255;
            if (opaque && runStart < 0)
            {
                runStart = x;
            }
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
