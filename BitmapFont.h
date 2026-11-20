#pragma once

#include <deki/providers/Buffer.h>

#include <cstdint>
#include <string>
#include <deki/assets/Texture2D.h>

namespace Deki2D
{

class OutlineFont;

/// Where one character sits in the atlas and how to place it.
struct GlyphInfo
{
    uint16_t x;       // X position in atlas
    uint16_t y;       // Y position in atlas
    uint8_t width;    // Glyph width in pixels
    uint8_t height;   // Glyph height in pixels
    int8_t offsetX;   // X offset when drawing
    int8_t offsetY;   // Y offset when drawing, from the baseline
    uint8_t advance;  // How far the cursor moves after this glyph
};

/// .dfont header, version 1: ASCII only, one glyph per character in a
/// contiguous range.
///
/// File layout: [FontHeader][GlyphInfo array][atlasPath, null-terminated]
///
/// The atlas is a separate .tex file named by a relative path.
struct FontHeader
{
    char magic[4];          // "DFNT"
    uint32_t version;       // 1
    uint8_t firstChar;      // First ASCII character, usually 32 (space)
    uint8_t lastChar;       // Last ASCII character, usually 126 (~)
    uint8_t lineHeight;     // Height of a line of text
    uint8_t baseline;       // Y offset from top to baseline
    uint16_t glyphCount;    // lastChar - firstChar + 1
    uint16_t atlasPathLen;  // Length of the atlas path, including the null terminator
};

/// .dfont header, version 2: Unicode, with a sparse codepoint table.
///
/// File layout: [FontHeaderV2][uint32_t codepoints[glyphCount]][GlyphInfo array][atlasPath]
///
/// Each glyph has a matching codepoint entry. The table is sorted, so lookup
/// is a binary search.
struct FontHeaderV2
{
    char magic[4];            // "DFNT"
    uint32_t version;         // 2
    uint32_t firstCodepoint;  // First codepoint, for showing the range only
    uint32_t lastCodepoint;   // Last codepoint, for showing the range only
    uint8_t lineHeight;       // Height of a line of text
    uint8_t baseline;         // Y offset from top to baseline
    uint16_t glyphCount;      // Number of glyphs actually included
    uint16_t atlasPathLen;    // Length of the atlas path, including the null terminator
    uint16_t reserved;        // Padding for alignment
};

/// Decoration baked into a v4+ font. Matches FontCompiler::DecorationMode.
///
/// When it is not None, the atlas stores 4-bit palette indices (in the low
/// nibble of each byte) instead of 8-bit alpha, and each TextComponent builds a
/// 16-entry colour palette from textColor and decorationColor.
enum class FontDecorationMode : uint8_t
{
    None = 0,
    Outline = 1,
    Shadow = 2
};

/// .dfont header, version 3: adds cap-height and x-height.
///
/// Contiguous ASCII layout: [FontHeaderV3][GlyphInfo array][atlasPath]
///
/// Sparse layout (high bit of version set):
/// [FontHeaderV3][uint32_t codepoints[glyphCount]][GlyphInfo array][atlasPath]
///
/// Cap-height and x-height allow vertical centring that does not depend on the
/// text shown (see BitmapFont::GetCapCenterY / GetXCenterY).
struct FontHeaderV3
{
    char magic[4];     // "DFNT"
    uint32_t version;  // 3 (contiguous ASCII) or 0x80000003 (sparse)
    uint32_t firstCodepoint;
    uint32_t lastCodepoint;
    uint8_t lineHeight;
    uint8_t baseline;
    uint8_t capHeight;  // Height of capital letters above the baseline (0 = unknown)
    uint8_t xHeight;    // Height of lowercase 'x' above the baseline (0 = unknown)
    uint16_t glyphCount;
    uint16_t atlasPathLen;
};

/// .dfont header, version 4: V3 plus four bytes describing the NDS-style
/// decoration baked into the atlas.
///
/// When decorationMode is not None, the atlas bytes are 4-bit palette indices
/// (low nibble per pixel) and TextComponent draws through a palette lookup.
/// The high bit of `version` marks a sparse font, as in v3.
struct FontHeaderV4
{
    char magic[4];     // "DFNT"
    uint32_t version;  // 4 or 0x80000004 (sparse)
    uint32_t firstCodepoint;
    uint32_t lastCodepoint;
    uint8_t lineHeight;
    uint8_t baseline;
    uint8_t capHeight;
    uint8_t xHeight;
    uint8_t decorationMode;  // FontDecorationMode value
    int8_t decorationA;      // Outline size (1..3) or shadow dx (-3..+3)
    int8_t decorationB;      // Shadow dy (-3..+3); unused for outline
    uint8_t reserved;        // Padding for uint16 alignment
    uint16_t glyphCount;
    uint16_t atlasPathLen;
};

/// A bitmap font: glyph metrics for text layout plus a texture atlas (.tex)
/// holding the glyph images.
class BitmapFont
{
public:
    /// Decodes one UTF-8 sequence at str[i] and advances i past it. Returns
    /// the codepoint, or 0xFFFD (after advancing one byte) on an invalid
    /// sequence. Text layout and measurement both use this one decoder.
    static uint32_t DecodeUtf8(const char* str, size_t len, size_t& i);

    /// Asset type name for AssetManager::Load<T>() lookup
    static constexpr const char* kAssetTypeName = "BitmapFont";

    BitmapFont();
    ~BitmapFont();

    /// Loads a font from a .dfont file. Returns nullptr on failure.
    static BitmapFont* Load(const char* filePath);

    /// Loads a font from the bytes of a .dfont file already in memory, such as
    /// one read from a pack file. Returns nullptr on failure.
    static BitmapFont* LoadFromFileData(const uint8_t* data, size_t size);

    /// Creates a monospace font from an atlas where every glyph is the same
    /// size and the glyphs sit in a grid, `charsPerRow` to a row, starting at
    /// `firstChar`. Returns nullptr on failure.
    static BitmapFont* CreateMonospace(const char* atlasPath, uint8_t glyphWidth, uint8_t glyphHeight,
                                       uint8_t firstChar, uint8_t charsPerRow, uint8_t charCount);

    /// Creates a font from glyph data and an atlas made at runtime, as the
    /// editor does from a TTF. Takes ownership of `atlas` and `glyphs`.
    /// Returns nullptr on failure.
    static BitmapFont* CreateFromMemory(Deki::Texture2D* atlas, Deki::Buffer<GlyphInfo>&& glyphs, uint8_t firstChar,
                                        uint8_t lastChar, uint8_t lineHeight, uint8_t baseline);

    /// The glyph for an ASCII character, or nullptr when the font lacks it.
    const GlyphInfo* GetGlyph(char c) const;

    /// The glyph for a Unicode codepoint (such as 0x4E00 for CJK), or nullptr
    /// when the font lacks it.
    const GlyphInfo* GetGlyphByCodepoint(uint32_t codepoint) const;

    /// Width of `text` in pixels.
    int32_t MeasureWidth(const char* text) const;

    /// Width in pixels of the first `length` bytes of `text`.
    int32_t MeasureWidth(const char* text, size_t length) const;

    /// Height of a line of text in pixels.
    uint8_t GetLineHeight() const { return m_LineHeight; }

    /// Y offset from the top of a line to the baseline.
    uint8_t GetBaseline() const { return m_Baseline; }

    /// Height of capital letters above the baseline. v1/v2 fonts do not store
    /// it, so it falls back to baseline * 7/10.
    uint8_t GetCapHeight() const;

    /// Height of lowercase 'x' above the baseline. v1/v2 fonts do not store
    /// it, so it falls back to baseline * 5/10.
    uint8_t GetXHeight() const;

    /// Y offset from the top of a line to the middle of the capital letters.
    /// Use it for UI labels: uppercase and mixed text looks centred whether or
    /// not it has descenders.
    int32_t GetCapCenterY() const;

    /// Y offset from the top of a line to the middle of lowercase 'x'. Use it
    /// for mostly lowercase body text.
    int32_t GetXCenterY() const;

    /// The baked decoration. With None the atlas is 8-bit alpha. With Outline
    /// or Shadow it stores 4-bit palette indices, and the renderer uses a
    /// 16-entry palette built from textColor and decorationColor.
    FontDecorationMode GetDecorationMode() const { return static_cast<FontDecorationMode>(m_DecorationMode); }

    /// Outline thickness (Outline) or shadow dx (Shadow).
    int8_t GetDecorationA() const { return m_DecorationA; }

    /// Shadow dy (Shadow); unused for Outline.
    int8_t GetDecorationB() const { return m_DecorationB; }

    /// The atlas texture, loaded on the first call.
    Deki::Texture2D* GetAtlas() const;

    /// The resolved atlas path.
    ///
    /// For a font from `Load(filePath)` it is the absolute path of the atlas
    /// .tex file (the .dfont's folder plus the file name in the header). For a
    /// font from `LoadFromFileData` it is the relative asset path used for pack
    /// lookups. Code that needs the raw atlas bytes (such as the editor
    /// preview) should read this path rather than parse the .dfont header
    /// again.
    const std::string& GetAtlasPath() const { return m_AtlasPath; }

    uint32_t GetFirstChar() const { return m_FirstChar; }

    uint32_t GetLastChar() const { return m_LastChar; }

    /// The vertical extent of all glyphs, relative to the baseline: `minY` is
    /// the topmost pixel and `maxY` the bottommost. Used for exact vertical
    /// centring.
    void GetVisualBounds(int32_t& minY, int32_t& maxY) const;

    /// Y offset from the top of a line to the middle of the glyphs' actual
    /// pixels, from GetVisualBounds.
    int32_t GetVisualCenterY() const;

    // ---- Fonts kept as outlines (a .dvec file), drawn at any size ----

    /// True for a font loaded from a .dvec file: it has no glyphs of its own,
    /// and AtSize gives the font for each size a text asks for.
    bool IsScalable() const { return m_Outline != nullptr; }

    /// The smallest and largest size AtSize draws: a glyph's size and offsets
    /// are kept in a byte.
    static constexpr int32_t kMinScalableSize = 4;
    static constexpr int32_t kMaxScalableSize = 96;

    /// This scalable font at `pixels` (its em that many pixels, as a TTF set
    /// to that size), made the first time and kept with it. Its metrics are
    /// all known at once; each glyph is drawn into its atlas the first time
    /// a text uses it (PrepareText). nullptr for a font that is not
    /// scalable.
    BitmapFont* AtSize(int32_t pixels) const;

    /// Draws the glyphs `text` uses that this font (one AtSize made) has not
    /// drawn yet. Nothing for any other font. The atlas can move as it grows,
    /// so call it before reading the atlas.
    void PrepareText(const char* text, size_t length) const;

private:
    // A scalable font made from .dvec bytes; nullptr when they are not one.
    static BitmapFont* FromOutlineData(const uint8_t* data, size_t size);
    // Draws glyph `index` of a sized font into its atlas.
    bool DrawGlyph(uint32_t index) const;

    struct SizedFont
    {
        int32_t pixels;
        BitmapFont* font;
    };

    OutlineFont* m_Outline = nullptr;          // a scalable font's outlines, owned
    const OutlineFont* m_SizedFrom = nullptr;  // the outlines a sized font draws from
    mutable Deki::Buffer<SizedFont> m_Sizes;   // a scalable font's sizes made so far
    float m_ScaleX = 0.0f;                     // font units to pixels, across
    float m_ScaleY = 0.0f;                     // and down, fitted so x-height is whole pixels
    mutable Deki::Buffer<uint8_t> m_Drawn;     // a sized font's glyphs: 1 once in the atlas
    mutable uint16_t m_ShelfX = 0;             // where the next glyph goes in the atlas
    mutable uint16_t m_ShelfY = 0;
    mutable uint16_t m_ShelfHeight = 0;

    mutable Deki::Texture2D* m_Atlas;  // Glyph atlas texture, loaded lazily
    // Owned buffers, so every loader can bail out on any error without
    // cleanup code.
    mutable Deki::Buffer<GlyphInfo> m_Glyphs;  // mutable: a sized outline font places glyphs as it draws them
    Deki::Buffer<uint32_t> m_Codepoints;       // Sorted; sparse fonts only
    uint32_t m_FirstChar;                      // First character code (32-bit for v2)
    uint32_t m_LastChar;                       // Last character code (32-bit for v2)
    uint8_t m_LineHeight;                      // Line height in pixels
    uint8_t m_Baseline;                        // Baseline offset
    uint8_t m_CapHeight;                       // Capital letter height above the baseline (0 = unknown, use fallback)
    uint8_t m_XHeight;                         // Lowercase 'x' height above the baseline (0 = unknown, use fallback)
    uint8_t m_DecorationMode;                  // v4+: FontDecorationMode (0 = None, the default)
    int8_t m_DecorationA;                      // v4+: outline size or shadow dx
    int8_t m_DecorationB;                      // v4+: shadow dy (unused for outline)
    uint16_t m_GlyphCount;
    bool m_IsSparse;          // True: sparse codepoint table. False: contiguous range
    std::string m_AtlasPath;  // Atlas path, kept for lazy loading

    // GetVisualBounds() scans every glyph. The glyph table never changes after
    // load, so the result is computed once and cached.
    mutable bool m_VisualBoundsValid = false;
    mutable int32_t m_VisualMinY = 0;
    mutable int32_t m_VisualMaxY = 0;
};

}  // namespace Deki2D
