#include "FontCompiler.h"

#ifdef DEKI_EDITOR

#include "BitmapFont.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Deki2D
{

bool FontCompiler::CompileTrueTypeFont(
    const std::string& ttfPath,
    const CompileOptions& options,
    CompileResult& outResult)
{
    if (ttfPath.empty() || !std::filesystem::exists(ttfPath))
        return false;

    // Initialize FreeType
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
        return false;

    FT_Face face;
    if (FT_New_Face(ft, ttfPath.c_str(), 0, &face))
    {
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, options.fontSize);

    // Get font-wide metrics for consistent baseline positioning
    int ascender = static_cast<int>(face->size->metrics.ascender >> 6);      // Max height above baseline
    int descender = -static_cast<int>(face->size->metrics.descender >> 6);   // Max depth below baseline (make positive)
    int cellHeight = ascender + descender;                                    // Uniform height for all glyphs
    int baseline = ascender;                                                  // Baseline position from top of cell

    const int firstChar = options.firstChar;
    const int lastChar = options.lastChar;
    const int charCount = lastChar - firstChar + 1;
    const int padding = options.padding;

    // Gather glyph data
    struct GlyphData
    {
        int width, height;
        int bearingX, bearingY;
        int advance;
        std::vector<uint8_t> bitmap;
    };
    std::vector<GlyphData> glyphData(charCount);

    int maxHeight = 0;
    int totalWidth = 0;

    for (int i = 0; i < charCount; i++)
    {
        char c = static_cast<char>(firstChar + i);
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
        {
            glyphData[i].width = 0;
            glyphData[i].height = 0;
            glyphData[i].bearingX = 0;
            glyphData[i].bearingY = 0;
            glyphData[i].advance = options.fontSize / 2;
            continue;
        }

        FT_GlyphSlot g = face->glyph;
        glyphData[i].width = g->bitmap.width;
        glyphData[i].height = g->bitmap.rows;
        glyphData[i].bearingX = g->bitmap_left;
        glyphData[i].bearingY = g->bitmap_top;
        glyphData[i].advance = static_cast<int>(g->advance.x >> 6);

        if (g->bitmap.buffer && g->bitmap.width > 0 && g->bitmap.rows > 0)
        {
            glyphData[i].bitmap.resize(g->bitmap.width * g->bitmap.rows);
            for (unsigned int row = 0; row < g->bitmap.rows; row++)
            {
                memcpy(glyphData[i].bitmap.data() + row * g->bitmap.width,
                       g->bitmap.buffer + row * g->bitmap.pitch,
                       g->bitmap.width);
            }
        }

        totalWidth += glyphData[i].width + padding * 2;
        if (glyphData[i].height > maxHeight)
            maxHeight = glyphData[i].height;
    }

    // Calculate atlas size
    int atlasWidth = 64;
    int targetArea = totalWidth * (maxHeight + padding * 2);
    int targetSide = static_cast<int>(std::sqrt(static_cast<double>(targetArea))) + 1;
    while (atlasWidth < targetSide) atlasWidth *= 2;
    atlasWidth = (std::max)(atlasWidth, 64);
    atlasWidth = (std::min)(atlasWidth, options.maxAtlasSize);

    // Pack glyphs tightly (original approach that works with TextComponent)
    outResult.glyphs.resize(charCount);
    int cursorX = padding;
    int cursorY = padding;
    int rowHeight = 0;

    for (int i = 0; i < charCount; i++)
    {
        int glyphW = glyphData[i].width + padding * 2;
        int glyphH = glyphData[i].height + padding * 2;

        if (cursorX + glyphW > atlasWidth)
        {
            cursorX = padding;
            cursorY += rowHeight;
            rowHeight = 0;
        }

        outResult.glyphs[i].x = static_cast<uint16_t>(cursorX);
        outResult.glyphs[i].y = static_cast<uint16_t>(cursorY);
        outResult.glyphs[i].width = static_cast<uint8_t>(glyphData[i].width);
        outResult.glyphs[i].height = static_cast<uint8_t>(glyphData[i].height);
        outResult.glyphs[i].offset_x = static_cast<int8_t>(glyphData[i].bearingX);
        // CSS model: offset_y = -bearingY (negative = glyph extends above baseline)
        outResult.glyphs[i].offset_y = static_cast<int8_t>(-glyphData[i].bearingY);
        outResult.glyphs[i].advance = static_cast<uint8_t>(glyphData[i].advance);

        cursorX += glyphW;
        if (glyphH > rowHeight)
            rowHeight = glyphH;
    }

    // Calculate final atlas height (power of 2)
    int atlasHeight = cursorY + rowHeight + padding;
    int finalHeight = 1;
    while (finalHeight < atlasHeight) finalHeight *= 2;
    atlasHeight = finalHeight;

    // Create atlas bitmap (RGBA)
    outResult.atlasRGBA.resize(atlasWidth * atlasHeight * 4, 0);

    for (int i = 0; i < charCount; i++)
    {
        if (glyphData[i].bitmap.empty())
            continue;

        int destX = outResult.glyphs[i].x;
        int destY = outResult.glyphs[i].y;

        for (int y = 0; y < glyphData[i].height; y++)
        {
            for (int x = 0; x < glyphData[i].width; x++)
            {
                int srcIdx = y * glyphData[i].width + x;
                int dstIdx = ((destY + y) * atlasWidth + (destX + x)) * 4;
                uint8_t alpha = glyphData[i].bitmap[srcIdx];
                outResult.atlasRGBA[dstIdx + 0] = 255;   // R
                outResult.atlasRGBA[dstIdx + 1] = 255;   // G
                outResult.atlasRGBA[dstIdx + 2] = 255;   // B
                outResult.atlasRGBA[dstIdx + 3] = alpha; // A
            }
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // Set result metadata
    outResult.atlasWidth = static_cast<uint32_t>(atlasWidth);
    outResult.atlasHeight = static_cast<uint32_t>(atlasHeight);
    outResult.firstChar = static_cast<uint8_t>(firstChar);
    outResult.lastChar = static_cast<uint8_t>(lastChar);
    outResult.lineHeight = static_cast<uint8_t>(cellHeight);
    outResult.baseline = static_cast<uint8_t>(baseline);

    return true;
}

bool FontCompiler::GenerateMonospaceGlyphs(
    int glyphWidth, int glyphHeight,
    int firstChar, int charCount, int charsPerRow,
    std::vector<GlyphInfo>& outGlyphs)
{
    if (glyphWidth <= 0 || glyphHeight <= 0 || charCount <= 0 || charsPerRow <= 0)
        return false;

    outGlyphs.resize(charCount);

    for (int i = 0; i < charCount; i++)
    {
        int col = i % charsPerRow;
        int row = i / charsPerRow;

        outGlyphs[i].x = static_cast<uint16_t>(col * glyphWidth);
        outGlyphs[i].y = static_cast<uint16_t>(row * glyphHeight);
        outGlyphs[i].width = static_cast<uint8_t>(glyphWidth);
        outGlyphs[i].height = static_cast<uint8_t>(glyphHeight);
        outGlyphs[i].offset_x = 0;
        outGlyphs[i].offset_y = 0;
        outGlyphs[i].advance = static_cast<uint8_t>(glyphWidth);
    }

    return true;
}

bool FontCompiler::WriteDfontFile(
    const std::string& path,
    const CompileResult& result,
    const std::string& atlasFilename)
{
    if (path.empty() || result.glyphs.empty())
        return false;

    // Ensure directory exists
    std::filesystem::path filePath(path);
    std::filesystem::create_directories(filePath.parent_path());

    FontHeader header;
    memcpy(header.magic, "DFNT", 4);
    header.version = 1;
    header.first_char = result.firstChar;
    header.last_char = result.lastChar;
    header.line_height = result.lineHeight;
    header.baseline = result.baseline;
    header.glyph_count = static_cast<uint16_t>(result.glyphs.size());
    header.atlas_path_len = static_cast<uint16_t>(atlasFilename.length() + 1);

    std::ofstream fontFile(path, std::ios::binary);
    if (!fontFile)
        return false;

    fontFile.write(reinterpret_cast<const char*>(&header), sizeof(FontHeader));
    fontFile.write(reinterpret_cast<const char*>(result.glyphs.data()),
                   result.glyphs.size() * sizeof(GlyphInfo));
    fontFile.write(atlasFilename.c_str(), atlasFilename.length() + 1);
    fontFile.close();

    return true;
}

} // namespace Deki2D

#endif // DEKI_EDITOR
