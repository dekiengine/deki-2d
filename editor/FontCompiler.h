#pragma once

#ifdef DEKI_EDITOR

#include <string>
#include <vector>
#include <cstdint>
#include "Deki2DModule.h"
#include "BitmapFont.h"  // For GlyphInfo

namespace Deki2D
{

/**
 * @brief Font compilation service for converting TTF fonts to bitmap fonts
 *
 * This service extracts shared font compilation logic used by both:
 * - RuntimeFontCache (on-demand font compilation for editor preview)
 * - FontImporterPanel (manual font import tool)
 * - FontSyncHandler (automatic font baking on asset sync)
 *
 * Usage:
 *   FontCompiler::CompileOptions options;
 *   options.fontSize = 16;
 *   FontCompiler::CompileResult result;
 *   if (FontCompiler::CompileTrueTypeFont(ttfPath, options, result)) {
 *       // Use result.atlasRGBA, result.glyphs, etc.
 *   }
 */
class DEKI_2D_API FontCompiler
{
public:
    /**
     * @brief FreeType hinting / rendering mode
     */
    enum class HintingMode
    {
        None,   // FT_LOAD_NO_HINTING  — unhinted, full grayscale AA
        Light,  // FT_LOAD_TARGET_LIGHT — no horizontal hinting, smoothest on low-DPI
        Normal, // FT_LOAD_TARGET_NORMAL — FreeType default hinting
        Mono    // FT_LOAD_TARGET_MONO  — 1-bit, no AA, pixel-crisp
    };

    /**
     * @brief Baked decoration kind.
     *
     * None: atlas stays 8-bit alpha (classic path). Outline/Shadow switch the atlas to
     * 4-bit palette-indexed, enabling NDS-style per-label colour swaps at render time.
     */
    enum class DecorationMode : uint8_t
    {
        None    = 0,
        Outline = 1,   // Glyph dilated by `outlineSize` px on all sides
        Shadow  = 2    // Secondary shape = glyph translated by (shadowDx, shadowDy)
    };

    /**
     * @brief Compilation options
     */
    struct CompileOptions
    {
        int fontSize = 16;
        int firstChar = 32;   // ASCII space
        int lastChar = 126;   // ASCII tilde
        int padding = 2;      // Padding around each glyph
        int maxAtlasSize = 2048;
        HintingMode hinting = HintingMode::Light;
        int oversample = 2;   // 1=off, 2/3/4=supersample at N×size then box-filter down. Forced to 1 for Mono.
        DecorationMode decoration = DecorationMode::None;
        int outlineSize = 1;  // Used when decoration == Outline. Valid 1..3.
        int shadowDx = 1;     // Used when decoration == Shadow. Valid -3..+3.
        int shadowDy = 1;     // Used when decoration == Shadow. Valid -3..+3.
    };

    /**
     * @brief Compilation result containing atlas and glyph data
     */
    struct CompileResult
    {
        std::vector<uint8_t> atlasRGBA;      // RGBA pixel data
        std::vector<GlyphInfo> glyphs;       // Glyph metrics
        std::vector<uint32_t> codepoints;    // Codepoint for each glyph (v2 sparse format)
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        uint32_t firstChar = 32;
        uint32_t lastChar = 126;
        uint8_t lineHeight = 0;
        uint8_t baseline = 0;
        uint8_t capHeight = 0;               // height of 'H' from baseline (0 = unknown)
        uint8_t xHeight = 0;                 // height of 'x' from baseline (0 = unknown)
        DecorationMode decoration = DecorationMode::None;
        int8_t decorationA = 0;              // outline size, or shadow dx
        int8_t decorationB = 0;              // shadow dy (unused for outline)
        bool isSparse = false;               // true = v2 sparse codepoint table
    };

    /**
     * @brief Compile a TrueType font to bitmap font data
     * @param ttfPath Path to the TTF/OTF file
     * @param options Compilation options (size, char range, padding)
     * @param outResult Output containing atlas and glyph data
     * @return true on success, false on failure
     */
    static bool CompileTrueTypeFont(
        const std::string& ttfPath,
        const CompileOptions& options,
        CompileResult& outResult
    );

    /**
     * @brief Generate glyph info for a monospace/grid-based font
     * @param glyphWidth Width of each glyph cell
     * @param glyphHeight Height of each glyph cell
     * @param firstChar First character code
     * @param charCount Number of characters
     * @param charsPerRow Characters per row in the atlas
     * @param outGlyphs Output glyph info array
     * @return true on success
     */
    static bool GenerateMonospaceGlyphs(
        int glyphWidth, int glyphHeight,
        int firstChar, int charCount, int charsPerRow,
        std::vector<GlyphInfo>& outGlyphs
    );

    /**
     * @brief BDF compilation options
     */
    struct BdfCompileOptions
    {
        std::vector<int> selectedChars;  // Codepoints to include (any Unicode range)
        int padding = 2;
        int maxAtlasSize = 2048;
        DecorationMode decoration = DecorationMode::None;
        int outlineSize = 1;   // 1..3 px when decoration == Outline
        int shadowDx = 1;      // -3..+3 px when decoration == Shadow
        int shadowDy = 1;      // -3..+3 px when decoration == Shadow
    };

    /**
     * @brief Compile a BDF bitmap font to bitmap font data
     * @param bdfPath Path to the .bdf file
     * @param options Compilation options (selected chars, padding)
     * @param outResult Output containing atlas and glyph data
     * @return true on success, false on failure
     */
    static bool CompileBdfFont(
        const std::string& bdfPath,
        const BdfCompileOptions& options,
        CompileResult& outResult
    );

    /**
     * @brief Get all codepoints available in a BDF file
     * @param bdfPath Path to the .bdf file
     * @return Vector of codepoints found in the file (empty on failure)
     */
    static std::vector<int> GetBdfCodepoints(const std::string& bdfPath);

    /**
     * @brief Write DFONT file from compilation result
     * @param path Output path for .dfont file
     * @param result Compilation result
     * @param atlasFilename Filename of the atlas (stored in header)
     * @return true on success
     */
    static bool WriteDfontFile(
        const std::string& path,
        const CompileResult& result,
        const std::string& atlasFilename
    );
};

} // namespace Deki2D

#endif // DEKI_EDITOR
