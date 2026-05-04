#pragma once

#include <cstdint>
#include <string>
#include "Texture2D.h"

/**
 * @brief Glyph metrics for a single character
 */
struct GlyphInfo
{
    uint16_t x;          // X position in atlas
    uint16_t y;          // Y position in atlas
    uint8_t width;       // Glyph width in pixels
    uint8_t height;      // Glyph height in pixels
    int8_t offset_x;     // X offset when rendering
    int8_t offset_y;     // Y offset when rendering (from baseline)
    uint8_t advance;     // How much to advance cursor after this glyph
};

/**
 * @brief Binary font format header (v1 - ASCII only, contiguous glyph array)
 *
 * File structure:
 * [FontHeader][GlyphInfo array][atlas_path (null-terminated string)]
 *
 * The atlas is a separate .tex file referenced by relative path.
 */
struct FontHeader
{
    char magic[4];           // "DFNT" (Deki Font)
    uint32_t version;        // Format version (1)
    uint8_t first_char;      // First ASCII character in font (usually 32 = space)
    uint8_t last_char;       // Last ASCII character in font (usually 126 = ~)
    uint8_t line_height;     // Height of a line of text
    uint8_t baseline;        // Y offset from top to baseline
    uint16_t glyph_count;    // Number of glyphs (last_char - first_char + 1)
    uint16_t atlas_path_len; // Length of atlas path string (including null terminator)
};

/**
 * @brief Binary font format header (v2 - Unicode, sparse codepoint table)
 *
 * File structure:
 * [FontHeaderV2][uint32_t codepoints[glyph_count]][GlyphInfo array][atlas_path]
 *
 * Glyphs are stored sparsely: each glyph has a corresponding codepoint entry.
 * Lookup requires searching the codepoint table (binary search, since sorted).
 */
struct FontHeaderV2
{
    char magic[4];           // "DFNT" (Deki Font)
    uint32_t version;        // Format version (2)
    uint32_t first_codepoint;// First codepoint (for info/range display)
    uint32_t last_codepoint; // Last codepoint (for info/range display)
    uint8_t line_height;     // Height of a line of text
    uint8_t baseline;        // Y offset from top to baseline
    uint16_t glyph_count;    // Number of glyphs (sparse - only included codepoints)
    uint16_t atlas_path_len; // Length of atlas path string (including null terminator)
    uint16_t reserved;       // Padding for alignment
};

/**
 * @brief Baked decoration kind for v4+ fonts. Matches FontCompiler::DecorationMode.
 *
 * When decoration != None the atlas stores 4-bit palette indices (in the low nibble
 * of each byte) instead of 8-bit alpha; the runtime builds a 16-entry colour palette
 * per TextComponent from textColor + decorationColor.
 */
enum class FontDecorationMode : uint8_t
{
    None    = 0,
    Outline = 1,
    Shadow  = 2
};

/**
 * @brief Binary font format header (v3 - adds cap-height / x-height metrics)
 *
 * File structure (contiguous ASCII):
 * [FontHeaderV3][GlyphInfo array][atlas_path]
 *
 * File structure (sparse, is_sparse flag via high bit of version):
 * [FontHeaderV3Sparse][uint32_t codepoints[glyph_count]][GlyphInfo array][atlas_path]
 *
 * Cap-height and x-height enable optical centering that is independent of the
 * actual text content (see BitmapFont::GetCapCenterY / GetXCenterY).
 */
struct FontHeaderV3
{
    char magic[4];           // "DFNT"
    uint32_t version;        // 3 (contiguous ASCII) or 0x80000003 (sparse)
    uint32_t first_codepoint;
    uint32_t last_codepoint;
    uint8_t line_height;
    uint8_t baseline;
    uint8_t cap_height;      // Height of capital letters from baseline (0 = unknown)
    uint8_t x_height;        // Height of lowercase 'x' from baseline (0 = unknown)
    uint16_t glyph_count;
    uint16_t atlas_path_len;
};

/**
 * @brief Binary font format header (v4 - adds NDS-style baked decoration metadata).
 *
 * Layout matches V3 plus four bytes describing the decoration baked into the atlas.
 * When `decoration_mode != None` the atlas bytes are 4-bit palette indices (low
 * nibble per pixel) and TextComponent switches to the palette-lookup render path.
 *
 * Sparse flag is carried in the high bit of `version` (0x80000004) just like v3.
 */
struct FontHeaderV4
{
    char magic[4];           // "DFNT"
    uint32_t version;        // 4 or 0x80000004 (sparse)
    uint32_t first_codepoint;
    uint32_t last_codepoint;
    uint8_t line_height;
    uint8_t baseline;
    uint8_t cap_height;
    uint8_t x_height;
    uint8_t decoration_mode; // FontDecorationMode enum value
    int8_t  decoration_a;    // outline size (1..3) OR shadow dx (-3..+3)
    int8_t  decoration_b;    // unused for outline OR shadow dy (-3..+3)
    uint8_t reserved;        // padding for uint16 alignment
    uint16_t glyph_count;
    uint16_t atlas_path_len;
};

/**
 * @brief Bitmap font for text rendering
 *
 * Supports ASCII characters with glyph metrics for proper text layout.
 * Uses a texture atlas (TEX format) for glyph rendering.
 */
class BitmapFont
{
public:
    /// Asset type name for AssetManager::Load<T>() lookup
    static constexpr const char* AssetTypeName = "BitmapFont";

    BitmapFont();
    ~BitmapFont();

    /**
     * @brief Load font from .dfont file
     * @param file_path Path to the .dfont file
     * @return Loaded font or nullptr on failure
     */
    static BitmapFont* Load(const char* file_path);

    /**
     * @brief Load font from raw file data in memory (for pack file support)
     * @param data Pointer to raw .dfont file bytes
     * @param size Total size in bytes
     * @return Loaded font or nullptr on failure
     */
    static BitmapFont* LoadFromFileData(const uint8_t* data, size_t size);

    /**
     * @brief Create a monospace font from a grid-based atlas
     *
     * This is a helper for simple fonts where all glyphs are the same size
     * and arranged in a grid pattern (like classic bitmap fonts).
     *
     * @param atlas_path Path to the atlas texture (.tex)
     * @param glyph_width Width of each glyph cell
     * @param glyph_height Height of each glyph cell
     * @param first_char First character code in the atlas
     * @param chars_per_row Number of characters per row in the atlas
     * @param char_count Total number of characters
     * @return Created font or nullptr on failure
     */
    static BitmapFont* CreateMonospace(const char* atlas_path,
                                       uint8_t glyph_width,
                                       uint8_t glyph_height,
                                       uint8_t first_char,
                                       uint8_t chars_per_row,
                                       uint8_t char_count);

    /**
     * @brief Create a font from pre-generated glyph data and atlas
     *
     * Used by the editor to create fonts from TTF at runtime.
     * Takes ownership of atlas and glyphs pointers.
     *
     * @param atlas Atlas texture (ownership transferred)
     * @param glyphs Array of glyph info (ownership transferred)
     * @param first_char First character code
     * @param last_char Last character code
     * @param line_height Height of a line of text
     * @param baseline Y offset from top to baseline
     * @return Created font or nullptr on failure
     */
    static BitmapFont* CreateFromMemory(Texture2D* atlas,
                                        GlyphInfo* glyphs,
                                        uint8_t first_char,
                                        uint8_t last_char,
                                        uint8_t line_height,
                                        uint8_t baseline);

    /**
     * @brief Get glyph info for a character (ASCII)
     * @param c Character to look up
     * @return Pointer to glyph info or nullptr if character not in font
     */
    const GlyphInfo* GetGlyph(char c) const;

    /**
     * @brief Get glyph info for a Unicode codepoint
     * @param codepoint Unicode codepoint (e.g., 0x4E00 for CJK)
     * @return Pointer to glyph info or nullptr if codepoint not in font
     */
    const GlyphInfo* GetGlyphByCodepoint(uint32_t codepoint) const;

    /**
     * @brief Measure the width of a text string
     * @param text Text to measure
     * @return Width in pixels
     */
    int32_t MeasureWidth(const char* text) const;

    /**
     * @brief Measure the width of a text string with length
     * @param text Text to measure
     * @param length Number of characters to measure
     * @return Width in pixels
     */
    int32_t MeasureWidth(const char* text, size_t length) const;

    /**
     * @brief Get line height
     * @return Height of a line of text in pixels
     */
    uint8_t GetLineHeight() const { return line_height; }

    /**
     * @brief Get baseline offset
     * @return Y offset from top to baseline
     */
    uint8_t GetBaseline() const { return baseline; }

    /**
     * @brief Height of capital letters from baseline.
     *        Falls back to (baseline * 7/10) for v1/v2 fonts that don't store it.
     */
    uint8_t GetCapHeight() const;

    /**
     * @brief Height of lowercase 'x' from baseline.
     *        Falls back to (baseline * 5/10) for v1/v2 fonts that don't store it.
     */
    uint8_t GetXHeight() const;

    /**
     * @brief Y offset from top-of-line to the optical cap-center.
     *        Use this for UI labels — uppercase/mixed text sits optically centered
     *        regardless of whether the text contains descenders.
     */
    int32_t GetCapCenterY() const;

    /**
     * @brief Y offset from top-of-line to the optical x-center.
     *        Use this for lowercase-heavy body text.
     */
    int32_t GetXCenterY() const;

    /**
     * @brief Baked decoration kind.
     *        None = atlas is 8-bit alpha (classic path).
     *        Outline / Shadow = atlas stores 4-bit palette indices; renderer uses
     *        a 16-entry palette built from textColor + decorationColor.
     */
    FontDecorationMode GetDecorationMode() const { return static_cast<FontDecorationMode>(decoration_mode); }

    /** @brief Decoration parameter A: outline thickness (Outline) or shadow dx (Shadow). */
    int8_t GetDecorationA() const { return decoration_a; }

    /** @brief Decoration parameter B: shadow dy (Shadow); unused for Outline. */
    int8_t GetDecorationB() const { return decoration_b; }

    /**
     * @brief Get the texture atlas (loads lazily on first call)
     * @return Pointer to atlas texture
     */
    Texture2D* GetAtlas() const;

    /**
     * @brief Get the resolved atlas path.
     *
     * For fonts loaded via `Load(file_path)` this is the absolute path to the
     * atlas .tex file (dfont's parent dir + filename from header). For fonts
     * loaded via `LoadFromFileData` this is the relative asset path used for
     * pack-reader lookups. Callers that need the raw atlas bytes (e.g. the
     * editor preview pipeline) should read from this path directly rather
     * than re-parsing the .dfont header.
     */
    const std::string& GetAtlasPath() const { return atlasPath_; }

    /**
     * @brief Get first character code in font
     */
    uint32_t GetFirstChar() const { return first_char; }

    /**
     * @brief Get last character code in font
     */
    uint32_t GetLastChar() const { return last_char; }

    /**
     * @brief Get the actual visual bounds of glyphs
     * @param min_y Output: minimum Y offset (topmost pixel relative to baseline)
     * @param max_y Output: maximum Y offset + height (bottommost pixel relative to baseline)
     *
     * This calculates the geometric bounds by examining all glyph offsets and heights,
     * useful for precise vertical centering.
     */
    void GetVisualBounds(int32_t& min_y, int32_t& max_y) const;

    /**
     * @brief Get the visual center Y offset
     * @return Y offset from top of line to visual center of glyphs
     *
     * Calculates the geometric vertical center based on actual glyph bounds.
     */
    int32_t GetVisualCenterY() const;

private:
    mutable Texture2D* atlas;  // Glyph atlas texture (lazy-loaded)
    GlyphInfo* glyphs;         // Array of glyph info
    uint32_t* codepoints;      // Sorted codepoint table (v2 only, nullptr for v1)
    uint32_t first_char;       // First character code (widened for v2)
    uint32_t last_char;        // Last character code (widened for v2)
    uint8_t line_height;       // Line height in pixels
    uint8_t baseline;          // Baseline offset
    uint8_t cap_height;        // Capital letter height from baseline (0 = unknown, use fallback)
    uint8_t x_height;          // Lowercase 'x' height from baseline (0 = unknown, use fallback)
    uint8_t decoration_mode;   // v4+: FontDecorationMode (0 = None; default)
    int8_t  decoration_a;      // v4+: outline size or shadow dx
    int8_t  decoration_b;      // v4+: shadow dy (unused for outline)
    uint16_t glyph_count;      // Number of glyphs
    bool is_sparse;            // true = v2 sparse codepoint table, false = v1 contiguous
    std::string atlasPath_;    // Deferred atlas path for lazy loading
};
