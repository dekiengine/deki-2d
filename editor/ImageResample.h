#pragma once

/**
 * @file ImageResample.h
 * @brief Shrinking an image for Max Size (editor only, header-only so the
 *        package's tests can reach it).
 *
 * Each region is resampled on its own: every sprite frame, and the nine
 * parts of a nine-slice image, into the stored rect its edges map to
 * (Sprite::SourceToStored, the rule the runtime uses). A stored pixel of one
 * frame is then made only of that frame's pixels, so neighbouring frames
 * cannot bleed into each other.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Deki2DEditor
{

struct PixelRect
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // [x0, x1) x [y0, y1)
};

/// round(v * stored / source), as Deki2D::Sprite::SourceToStored.
inline int MapEdge(int v, int sourceSize, int storedSize)
{
    if (sourceSize <= 0 || sourceSize == storedSize)
        return v;
    return static_cast<int>((static_cast<int64_t>(v) * storedSize * 2 + sourceSize) /
                            (2 * static_cast<int64_t>(sourceSize)));
}

/// Resample `from` (in an RGBA image srcW wide) into `to` (in one dstW wide).
/// Area average: each stored pixel is the coverage-weighted mean of the
/// source pixels under it, colour weighted by alpha so transparent pixels do
/// not darken an edge. `nearest` takes the pixel under the centre instead,
/// for chroma-keyed images, where a blend would invent colours next to the key.
inline void ResampleRect(const uint8_t* src, int srcW, const PixelRect& from, uint8_t* dst, int dstW,
                         const PixelRect& to, bool nearest)
{
    const int fw = from.x1 - from.x0, fh = from.y1 - from.y0;
    const int tw = to.x1 - to.x0, th = to.y1 - to.y0;
    if (fw <= 0 || fh <= 0 || tw <= 0 || th <= 0)
        return;
    const double sx = static_cast<double>(fw) / tw, sy = static_cast<double>(fh) / th;

    for (int y = 0; y < th; ++y)
    {
        const double fy0 = from.y0 + y * sy, fy1 = fy0 + sy;
        for (int x = 0; x < tw; ++x)
        {
            uint8_t* out = dst + (static_cast<size_t>(to.y0 + y) * dstW + (to.x0 + x)) * 4;
            const double fx0 = from.x0 + x * sx, fx1 = fx0 + sx;
            if (nearest)
            {
                const int px = std::min(from.x1 - 1, static_cast<int>((fx0 + fx1) * 0.5));
                const int py = std::min(from.y1 - 1, static_cast<int>((fy0 + fy1) * 0.5));
                const uint8_t* in = src + (static_cast<size_t>(py) * srcW + px) * 4;
                out[0] = in[0];
                out[1] = in[1];
                out[2] = in[2];
                out[3] = in[3];
                continue;
            }
            double r = 0, g = 0, b = 0, a = 0, cr = 0, cg = 0, cb = 0, area = 0;
            const int iy0 = static_cast<int>(std::floor(fy0)), iy1 = std::min(from.y1, static_cast<int>(std::ceil(fy1)));
            const int ix0 = static_cast<int>(std::floor(fx0)), ix1 = std::min(from.x1, static_cast<int>(std::ceil(fx1)));
            for (int iy = iy0; iy < iy1; ++iy)
            {
                const double wy = std::min(fy1, iy + 1.0) - std::max(fy0, static_cast<double>(iy));
                if (wy <= 0)
                    continue;
                for (int ix = ix0; ix < ix1; ++ix)
                {
                    const double wx = std::min(fx1, ix + 1.0) - std::max(fx0, static_cast<double>(ix));
                    if (wx <= 0)
                        continue;
                    const double w = wx * wy;
                    const uint8_t* in = src + (static_cast<size_t>(iy) * srcW + ix) * 4;
                    const double wa = w * in[3];
                    r += in[0] * wa;
                    g += in[1] * wa;
                    b += in[2] * wa;
                    a += wa;
                    cr += in[0] * w;
                    cg += in[1] * w;
                    cb += in[2] * w;
                    area += w;
                }
            }
            if (area <= 0)
                continue;
            if (a > 0)
            {
                out[0] = static_cast<uint8_t>(std::lround(r / a));
                out[1] = static_cast<uint8_t>(std::lround(g / a));
                out[2] = static_cast<uint8_t>(std::lround(b / a));
            }
            else
            {
                // Fully transparent: keep the plain average so the colour
                // under an alpha of 0 is still sensible if a format drops alpha.
                out[0] = static_cast<uint8_t>(std::lround(cr / area));
                out[1] = static_cast<uint8_t>(std::lround(cg / area));
                out[2] = static_cast<uint8_t>(std::lround(cb / area));
            }
            out[3] = static_cast<uint8_t>(std::lround(a / area));
        }
    }
}

/// The source rect in stored pixels, edges mapped.
inline PixelRect MapRect(const PixelRect& r, int srcW, int srcH, int dstW, int dstH)
{
    return { MapEdge(r.x0, srcW, dstW), MapEdge(r.y0, srcH, dstH), MapEdge(r.x1, srcW, dstW),
             MapEdge(r.y1, srcH, dstH) };
}

/// Shrink a whole RGBA image to dstW x dstH. `regions` are resampled on
/// their own after the whole image (frames, nine-slice parts), each into its
/// mapped rect; pixels outside every region come from the whole-image pass.
inline std::vector<uint8_t> ShrinkImage(const uint8_t* src, int srcW, int srcH, int dstW, int dstH,
                                        const std::vector<PixelRect>& regions, bool nearest)
{
    std::vector<uint8_t> dst(static_cast<size_t>(dstW) * dstH * 4, 0);
    ResampleRect(src, srcW, { 0, 0, srcW, srcH }, dst.data(), dstW, { 0, 0, dstW, dstH }, nearest);
    for (const PixelRect& r : regions)
    {
        PixelRect clamped{ std::max(0, r.x0), std::max(0, r.y0), std::min(srcW, r.x1), std::min(srcH, r.y1) };
        if (clamped.x1 <= clamped.x0 || clamped.y1 <= clamped.y0)
            continue;
        const PixelRect to = MapRect(clamped, srcW, srcH, dstW, dstH);
        if (to.x1 <= to.x0 || to.y1 <= to.y0)
            continue;
        ResampleRect(src, srcW, clamped, dst.data(), dstW, to, nearest);
    }
    return dst;
}

}  // namespace Deki2DEditor
