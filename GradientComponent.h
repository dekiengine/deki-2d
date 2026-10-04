#pragma once

#include <deki/providers/Buffer.h>

#include <stdint.h>
#include "deki-rendering/RendererComponent.h"
#include <deki/Color.h>

namespace Deki2D
{

enum class GradientType : uint8_t
{
    Linear = 0,  // Along a direction set by the angle
    Radial = 1,  // Out from the centre
    Conical = 2  // Around the centre
};

enum class GradientTileMode : uint8_t
{
    None = 0,  // Drawn once
    Horizontal = 1,
    Vertical = 2,
    Both = 3,
    Mirror = 4  // Every other tile is reversed
};

enum class GradientDitherMode : uint8_t
{
    None = 0,
    Ordered2x2 = 1,   // 2x2 Bayer matrix (coarse pattern)
    Ordered4x4 = 2,   // 4x4 Bayer matrix
    Ordered8x8 = 3,   // 8x8 Bayer matrix
    Ordered16x16 = 4  // 16x16 Bayer matrix (finest pattern)
};

/// One colour stop of a gradient.
struct GradientStop
{
    float position;  // Along the gradient, 0 to 1
    Deki::Color color;

    GradientStop(float pos = 0.0f, const Deki::Color& col = Deki::Color::Black)
        : position(pos),
          color(col)
    {
    }

    // From separate 0-255 channels
    GradientStop(float pos, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : position(pos),
          color(red, green, blue, alpha)
    {
    }
};

/// Draws a procedural gradient: linear, radial or conical, with optional
/// tiling, and dithering to hide banding on RGB565 displays. The gradient is
/// baked into a pixel buffer and re-baked only when an input changes.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Draws a procedural gradient: linear, radial or conical.")
DEKI_FORMER_NAME("GradientComponent")
class GradientComponent : public DekiRendering::RendererComponent
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("Linear runs the colours along the angle below. Radial runs them out from the centre point.")
    GradientType gradientType;
    DEKI_EXPORT
    DEKI_TOOLTIP("What happens past the last stop: hold the end colour, repeat the ramp, or mirror it back.")
    GradientTileMode tileMode;
    DEKI_EXPORT
    DEKI_TOOLTIP(
        "Dithering hides the banding a smooth ramp shows on a 16-bit display, by trading it for a fine speckle.")
    GradientDitherMode ditherMode;

    // Size of one Bayer cell, in art pixels. 1 (the default) dithers pixel by
    // pixel; 2, 4, 8 or 16 make chunkier blocks for a retro look. Other values
    // snap down to a power of two (3 becomes 2, 7 becomes 4).
    DEKI_EXPORT
    DEKI_TOOLTIP("Size of the dither pattern in art pixels. 1 dithers pixel by pixel; 2, 4, 8 or 16 make chunkier "
                 "blocks for a retro look.")
    uint8_t ditherScale;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the gradient in meters.")
    DEKI_UNIT(Distance)
    float width;
    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the gradient in meters.")
    DEKI_UNIT(Distance)
    float height;

    DEKI_EXPORT
    DEKI_TOOLTIP("Direction a linear gradient runs, in radians. 0 runs left to right.")
    DEKI_UNIT(Angle)
    float angle;  // Radians (0 = horizontal, pi/2 = vertical); the inspector shows degrees
    DEKI_EXPORT
    DEKI_TOOLTIP("Centre of a radial gradient across the width, 0 to 1. 0.5 is the middle.")
    float centerX;  // Radial and conical, 0 to 1
    DEKI_EXPORT
    DEKI_TOOLTIP("Centre of a radial gradient down the height, 0 to 1.")
    float centerY;  // Radial and conical, 0 to 1
    DEKI_EXPORT
    DEKI_TOOLTIP("How far a radial gradient reaches before the last stop, relative to the size.")
    float radius;  // Radial, 0 to 1

    // At most 4 stops, to keep the component small
    static constexpr uint8_t kMaxStops = 4;
    GradientStop stops[kMaxStops];

    DEKI_EXPORT
    DEKI_TOOLTIP("How many of the four colour stops are used. The rest are ignored.")
    uint8_t stopCount;

    // Each stop as separate properties, for the inspector and serialization.
    // Stop 1 is always shown: a gradient needs at least one stop.
    DEKI_EXPORT
    DEKI_TOOLTIP("Where the first stop sits along the ramp, 0 to 1.")
    float stop1Position;
    DEKI_EXPORT
    DEKI_TOOLTIP("Colour at the first stop.")
    Deki::Color stop1Color;

    DEKI_EXPORT
    DEKI_TOOLTIP("Where the second stop sits along the ramp, 0 to 1.")
    DEKI_VISIBLE_WHEN(stopCount, 2)
    float stop2Position;
    DEKI_EXPORT
    DEKI_TOOLTIP("Colour at the second stop.")
    DEKI_VISIBLE_WHEN(stopCount, 2)
    Deki::Color stop2Color;

    DEKI_EXPORT
    DEKI_TOOLTIP("Where the third stop sits, 0 to 1. Used when the stop count is 3 or more.")
    DEKI_VISIBLE_WHEN(stopCount, 3)
    float stop3Position;
    DEKI_EXPORT
    DEKI_TOOLTIP("Colour at the third stop.")
    DEKI_VISIBLE_WHEN(stopCount, 3)
    Deki::Color stop3Color;

    DEKI_EXPORT
    DEKI_TOOLTIP("Where the fourth stop sits, 0 to 1. Used when the stop count is 4.")
    DEKI_VISIBLE_WHEN(stopCount, 4)
    float stop4Position;
    DEKI_EXPORT
    DEKI_TOOLTIP("Colour at the fourth stop.")
    DEKI_VISIBLE_WHEN(stopCount, 4)
    Deki::Color stop4Color;

    // Tile size in world meters; 0 uses the component's width or height
    DEKI_EXPORT
    DEKI_TOOLTIP("Width of one repeat when the tile mode repeats or mirrors.")
    DEKI_UNIT(Distance)
    float tileWidth;
    DEKI_EXPORT
    DEKI_TOOLTIP("Height of one repeat when the tile mode repeats or mirrors.")
    DEKI_UNIT(Distance)
    float tileHeight;

    GradientComponent(float w = 0.0f, float h = 0.0f);
    ~GradientComponent();

    void SetGradientType(GradientType type);

    /// Makes this a linear gradient at `angleRadians` (0 runs left to right,
    /// pi/2 top to bottom).
    void SetLinearGradient(float angleRadians = 0.0f);

    /// Makes this a radial gradient. Centre and radius are 0 to 1, relative to
    /// the size, and are clamped to that range.
    void SetRadialGradient(float centerX = 0.5f, float centerY = 0.5f, float radius = 0.5f);

    /// Adds a colour stop at `position` (0 to 1) and keeps the stops sorted.
    /// Does nothing when all four stops are in use.
    void AddColorStop(float position, uint8_t r, uint8_t g, uint8_t b);

    void ClearColorStops();

    /// Copies the stopN properties into stops[]. Runs before every bake.
    void SyncStopsFromProperties();

    /// Replaces the stops with two: the start colour at 0 and the end colour
    /// at 1.
    void SetSimpleGradient(uint8_t startR, uint8_t startG, uint8_t startB, uint8_t endR, uint8_t endG, uint8_t endB);

    /// Sets the tiling mode and tile size in world meters (0 uses the
    /// component's width or height).
    void SetTiling(GradientTileMode mode, float tileW = 0.0f, float tileH = 0.0f);

    void SetDithering(GradientDitherMode mode);

    /// Sets the size, in world meters.
    void SetArea(float w, float h);

    /// Bakes the gradient into an RGB565 buffer of outW x outH pixels
    /// (outW * outH * 2 bytes).
    ///
    /// The gradient is laid out on its art grid (artW x artH: its size at the
    /// project's pixels per meter); each output pixel takes the art position
    /// it covers. The dither pattern is laid out in output pixels, in cells
    /// `ditherCell` wide. Baked at the density it is drawn at, a gradient
    /// lands 1:1 on the screen at any scale, so the pattern stays regular
    /// where a scaled bake would repeat or drop pixels and show seams. At a
    /// whole-number scale the result is exactly the art-grid bake, scaled.
    void RenderToBuffer(uint8_t* buffer, int32_t outW, int32_t outH, int32_t artW, int32_t artH, int32_t ditherCell);

    // Drawn through QuadBlit.
    // Culling extents in meters: the box the content is baked into.
    bool GetContentExtents(float& outWidth, float& outHeight) const override
    {
        if (width <= 0.0f || height <= 0.0f)
        {
            return false;
        }
        outWidth = width;
        outHeight = height;
        return true;
    }

    bool RenderContent(const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX, float& outPivotY,
                       uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB, uint8_t& outTintA) override;

private:
    /// The position along the gradient (0 to 1) of a point given in 0 to 1
    /// coordinates across the area.
    float CalculateGradientPosition(float normX, float normY) const;

    /// The colour at `position` (0 to 1), blended between the two stops
    /// around it.
    void InterpolateColor(float position, uint8_t* r, uint8_t* g, uint8_t* b) const;

    /// The Bayer threshold at (x, y) for the current ditherMode, in [0, 1).
    float SampleBayerThreshold(int32_t x, int32_t y) const;

    /// Pixelorama-style stipple: picks one of the two stops around `position`,
    /// by whether the position between them reaches `threshold`. Every output
    /// pixel is exactly one of the stop colours.
    void PickStopByThreshold(float position, float threshold, uint8_t* r, uint8_t* g, uint8_t* b) const;

    uint16_t ConvertToRGB565(uint8_t r, uint8_t g, uint8_t b) const;

    /// Writes one pixel, skipping it when it is outside the buffer.
    void RenderPixel(int32_t x, int32_t y, uint16_t color, uint8_t* renderBuffer, int screenWidth,
                     int screenHeight) const;

    // Baked pixels for the current properties, which RenderContent hands to
    // QuadBlit. Keyed by a hash of every input, so the gradient is rasterised
    // (with trig per pixel) only when something changed.
    Deki::Buffer<uint8_t> m_Baked;
    uint64_t m_BakeKey = 0;
    // A bake size the device could not allocate. Retrying it every frame
    // achieves nothing and floods the log, so it is attempted once and
    // only reconsidered when the required size changes.
    size_t m_BakeFailedSize = 0;
    uint64_t ComputeBakeKey(int32_t widthPx, int32_t heightPx) const;

    // Linear-gradient trig, computed once per bake instead of once per pixel.
    float m_CosAngle = 1.0f;
    float m_SinAngle = 0.0f;

    // Copies stops[] back into the stopN properties after a change from code,
    // so SyncStopsFromProperties(), which runs before every bake, does not
    // overwrite AddColorStop()/SetSimpleGradient() with stale values.
    void WriteStopsToProperties();
};

}  // namespace Deki2D
