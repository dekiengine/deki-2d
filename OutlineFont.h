#pragma once

// A font kept as the outlines of its letters, so a text can be drawn at any
// size on the device. The editor makes the file from a TTF or OTF; each glyph
// is drawn the first time a text at some size needs it (BitmapFont::AtSize).

#include <deki/providers/Buffer.h>

#include <cstddef>
#include <cstdint>

namespace Deki2D
{

/// .dvec file layout, all little-endian:
///
/// [OutlineFontHeader][uint32_t codepoints[glyphCount], sorted]
/// [OutlineGlyph glyphs[glyphCount]][uint16_t contourEnds[contourCount]]
/// [OutlinePoint points[pointCount]]
///
/// Coordinates are font units, y up from the baseline. Each glyph's contours
/// and points are a run of the shared tables; a contour end is the index of
/// its last point, counted from the glyph's first.
struct OutlineFontHeader
{
    char magic[4];        // "DVEC"
    uint32_t version;     // 1
    uint16_t unitsPerEm;  // font units in one em: a size of N pixels makes an em N pixels
    int16_t ascender;     // above the baseline
    int16_t descender;    // below it, negative
    int16_t capHeight;    // top of 'H'
    int16_t xHeight;      // top of 'x'
    uint16_t reserved;
    uint32_t glyphCount;
    uint32_t contourCount;
    uint32_t pointCount;
};

struct OutlineGlyph
{
    uint32_t firstContour;
    uint32_t firstPoint;
    uint16_t contourCount;
    uint16_t advance;  // font units
    int16_t xMin;
    int16_t yMin;
    int16_t xMax;
    int16_t yMax;
};

/// What a point is: on the outline, or a control point that bends it.
enum class OutlinePointKind : uint8_t
{
    On = 0,
    Quadratic = 1,  // TrueType's: one between two points on the outline
    Cubic = 2       // CFF's: two in a row
};

struct OutlinePoint
{
    int16_t x;
    int16_t y;
    uint8_t kind;  // OutlinePointKind
    uint8_t reserved;
};

class OutlineFont
{
public:
    /// The font in a .dvec file's bytes, copied, or nullptr when they are
    /// not one or do not hold together.
    static OutlineFont* FromFileData(const uint8_t* data, size_t size);

    /// True when `data` starts like a .dvec file.
    static bool IsOutlineFile(const uint8_t* data, size_t size);

    const OutlineFontHeader& Header() const { return m_Header; }

    uint32_t GlyphCount() const { return m_Header.glyphCount; }

    /// The codepoints, sorted: glyph `i` draws codepoints()[i].
    const uint32_t* Codepoints() const { return m_Codepoints; }

    const OutlineGlyph& Glyph(uint32_t index) const { return m_Glyphs[index]; }

    /// Adds glyph `index` into an 8-bit coverage image `width` x `height`
    /// (rows `stride` bytes apart), the image already cleared. A font unit
    /// at (x, y) lands at pixel (originX + x * scaleX, originY - y * scaleY):
    /// y runs down the image. Each pixel's coverage is the share of its area
    /// inside the outline, so edges come out smooth. Returns false when the
    /// glyph's scratch space could not be had.
    bool Rasterize(uint32_t index, float scaleX, float scaleY, float originX, float originY, uint8_t* image,
                   int32_t width, int32_t height, int32_t stride) const;

private:
    OutlineFont() = default;

    // Adds the signed area a line from (x0, y0) to (x1, y1) covers to the
    // accumulation rows (Raph Levien's font-rs method).
    void AddLine(float x0, float y0, float x1, float y1) const;
    void AddQuadratic(float x0, float y0, float cx, float cy, float x1, float y1) const;
    void AddCubic(float x0, float y0, float c1x, float c1y, float c2x, float c2y, float x1, float y1) const;

    Deki::Buffer<uint8_t> m_Data;
    OutlineFontHeader m_Header{};
    const uint32_t* m_Codepoints = nullptr;
    const OutlineGlyph* m_Glyphs = nullptr;
    const uint16_t* m_ContourEnds = nullptr;
    const OutlinePoint* m_Points = nullptr;

    // Scratch for one glyph: (width + 2) x height sums, kept between glyphs.
    mutable Deki::Buffer<float> m_Accumulation;
    mutable int32_t m_AccWidth = 0;
    mutable int32_t m_AccHeight = 0;
};

}  // namespace Deki2D
