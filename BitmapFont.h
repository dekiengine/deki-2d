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
     * @brief Get the texture atlas (loads lazily on first call)
     * @return Pointer to atlas texture
     */
    Texture2D* GetAtlas() const;

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
    uint16_t glyph_count;      // Number of glyphs
    bool is_sparse;            // true = v2 sparse codepoint table, false = v1 contiguous
    std::string atlasPath_;    // Deferred atlas path for lazy loading
};
