#pragma once

#ifdef DEKI_EDITOR

#include <string>
#include <vector>
#include <cstdint>
#include "Deki2DPackage.h"
#include "BitmapFont.h"  // For GlyphInfo

namespace Deki2D
{

/// Compiles TrueType and BDF fonts into bitmap fonts (an atlas plus glyph
/// metrics). Shared by the editor's font preview, the font inspectors and
/// FontSyncHandler, which bakes fonts when assets sync.
///
///     FontCompiler::CompileOptions options;
///     options.fontSize = 16;
///     FontCompiler::CompileResult result;
///     if (FontCompiler::CompileTrueTypeFont(ttfPath, options, result)) {
///         // Use result.atlasRGBA, result.glyphs, etc.
///     }
class DEKI_2D_API FontCompiler
{
public:
    /// FreeType hinting and rendering mode.
    enum class HintingMode
    {
        None,    // FT_LOAD_NO_HINTING: unhinted, full grayscale AA
        Light,   // FT_LOAD_TARGET_LIGHT: no horizontal hinting, smoothest on low-DPI
        Normal,  // FT_LOAD_TARGET_NORMAL: FreeType default hinting
        Mono     // FT_LOAD_TARGET_MONO: 1-bit, no AA, pixel-crisp
    };

    /// Decoration baked into the atlas. With None the atlas is 8-bit alpha.
    /// Outline and Shadow make it 4-bit palette indices, so each label can
    /// swap colours at render time, as on the Nintendo DS.
    enum class DecorationMode : uint8_t
    {
        None = 0,
        Outline = 1,  // Glyph dilated by `outlineSize` px on all sides
        Shadow = 2    // the glyph's shape moved by (shadowDx, shadowDy)
    };

    struct CompileOptions
    {
        int fontSize = 16;
        int firstChar = 32;  // ASCII space
        int lastChar = 126;  // ASCII tilde
        int padding = 2;     // around each glyph
        int maxAtlasSize = 2048;
        HintingMode hinting = HintingMode::Light;
        int oversample = 2;  // 1=off, 2/3/4=render at N times the size, then box-filter down. Always 1 for Mono.
        DecorationMode decoration = DecorationMode::None;
        int outlineSize = 1;  // Used when decoration == Outline. Valid 1..3.
        int shadowDx = 1;     // Used when decoration == Shadow. Valid -3..+3.
        int shadowDy = 1;     // Used when decoration == Shadow. Valid -3..+3.
    };

    struct CompileResult
    {
        std::vector<uint8_t> atlasRGBA;  // RGBA pixel data
        std::vector<GlyphInfo> glyphs;
        std::vector<uint32_t> codepoints;  // Codepoint for each glyph (v2 sparse format)
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        uint32_t firstChar = 32;
        uint32_t lastChar = 126;
        uint8_t lineHeight = 0;
        uint8_t baseline = 0;
        uint8_t capHeight = 0;  // height of 'H' from baseline (0 = unknown)
        uint8_t xHeight = 0;    // height of 'x' from baseline (0 = unknown)
        DecorationMode decoration = DecorationMode::None;
        int8_t decorationA = 0;  // outline size, or shadow dx
        int8_t decorationB = 0;  // shadow dy (unused for outline)
        bool isSparse = false;   // true = v2 sparse codepoint table
    };

    /// Compiles a TTF/OTF file into `outResult`. Returns false on failure.
    static bool CompileTrueTypeFont(const std::string& ttfPath, const CompileOptions& options,
                                    CompileResult& outResult);

    /// Glyph info for a monospace font laid out as a grid of equal cells,
    /// charsPerRow to a row of the atlas.
    static bool GenerateMonospaceGlyphs(int glyphWidth, int glyphHeight, int firstChar, int charCount, int charsPerRow,
                                        std::vector<GlyphInfo>& outGlyphs);

    struct BdfCompileOptions
    {
        std::vector<int> selectedChars;  // Codepoints to include (any Unicode range)
        int padding = 2;
        int maxAtlasSize = 2048;
        DecorationMode decoration = DecorationMode::None;
        int outlineSize = 1;  // 1..3 px when decoration == Outline
        int shadowDx = 1;     // -3..+3 px when decoration == Shadow
        int shadowDy = 1;     // -3..+3 px when decoration == Shadow
    };

    /// Compiles a .bdf file into `outResult`. Returns false on failure.
    static bool CompileBdfFont(const std::string& bdfPath, const BdfCompileOptions& options, CompileResult& outResult);

    /// Every codepoint in a .bdf file; empty on failure.
    static std::vector<int> GetBdfCodepoints(const std::string& bdfPath);

    /// Writes a .dfont file. `atlasFilename` names the atlas and goes in the
    /// header. Returns false on failure.
    static bool WriteDfontFile(const std::string& path, const CompileResult& result, const std::string& atlasFilename);
};

}  // namespace Deki2D

#endif  // DEKI_EDITOR
