#include "FontCompiler.h"

#ifdef DEKI_EDITOR

#include "BitmapFont.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <set>
#include <unordered_map>

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

    // Sort glyphs by height (tallest first) for tighter shelf packing
    std::vector<int> sortedIndices(charCount);
    for (int i = 0; i < charCount; i++) sortedIndices[i] = i;
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        return glyphData[a].height > glyphData[b].height;
    });

    // Try multiple atlas widths and pick the one that minimizes total area
    int bestWidth = 64;
    int bestArea = INT_MAX;
    for (int tryWidth = 64; tryWidth <= options.maxAtlasSize; tryWidth *= 2)
    {
        int cx = padding, cy = padding, rh = 0;
        for (int idx : sortedIndices)
        {
            int gw = glyphData[idx].width + padding * 2;
            int gh = glyphData[idx].height + padding * 2;
            if (cx + gw > tryWidth) { cx = padding; cy += rh; rh = 0; }
            cx += gw;
            if (gh > rh) rh = gh;
        }
        int h = cy + rh + padding;
        int area = tryWidth * h;
        if (area < bestArea) { bestArea = area; bestWidth = tryWidth; }
    }
    int atlasWidth = bestWidth;

    // Pack glyphs in height-sorted order for tight shelf packing
    outResult.glyphs.resize(charCount);
    int cursorX = padding;
    int cursorY = padding;
    int rowHeight = 0;

    for (int idx : sortedIndices)
    {
        int glyphW = glyphData[idx].width + padding * 2;
        int glyphH = glyphData[idx].height + padding * 2;

        if (cursorX + glyphW > atlasWidth)
        {
            cursorX = padding;
            cursorY += rowHeight;
            rowHeight = 0;
        }

        outResult.glyphs[idx].x = static_cast<uint16_t>(cursorX);
        outResult.glyphs[idx].y = static_cast<uint16_t>(cursorY);
        outResult.glyphs[idx].width = static_cast<uint8_t>(glyphData[idx].width);
        outResult.glyphs[idx].height = static_cast<uint8_t>(glyphData[idx].height);
        outResult.glyphs[idx].offset_x = static_cast<int8_t>(glyphData[idx].bearingX);
        // CSS model: offset_y = -bearingY (negative = glyph extends above baseline)
        outResult.glyphs[idx].offset_y = static_cast<int8_t>(-glyphData[idx].bearingY);
        outResult.glyphs[idx].advance = static_cast<uint8_t>(glyphData[idx].advance);

        cursorX += glyphW;
        if (glyphH > rowHeight)
            rowHeight = glyphH;
    }

    // Final atlas height — exact fit, no power-of-2 rounding
    int atlasHeight = cursorY + rowHeight + padding;

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

// =========================================================================
// BDF Font Compilation
// =========================================================================

namespace
{

struct BdfGlyph
{
    int encoding = -1;
    int dwidthX = 0;
    int bbxW = 0, bbxH = 0;
    int bbxOffX = 0, bbxOffY = 0;
    std::vector<uint8_t> bitmap; // 1 byte per pixel (0 or 255)
};

struct BdfFontData
{
    int ascent = 0;
    int descent = 0;
    std::vector<BdfGlyph> glyphs;
};

static uint8_t HexCharToNibble(char c)
{
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    return 0;
}

static bool ParseBdfFile(const std::string& path, BdfFontData& outFont)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::string line;
    BdfGlyph currentGlyph;
    bool inChar = false;
    bool inBitmap = false;
    int bitmapRow = 0;

    while (std::getline(file, line))
    {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
            continue;

        if (inBitmap)
        {
            if (line == "ENDCHAR")
            {
                inBitmap = false;
                inChar = false;
                outFont.glyphs.push_back(std::move(currentGlyph));
                currentGlyph = BdfGlyph();
                continue;
            }

            // Decode hex row into bitmap pixels
            int bytesPerRow = (currentGlyph.bbxW + 7) / 8;
            for (int byteIdx = 0; byteIdx < bytesPerRow && (byteIdx * 2 + 1) < (int)line.size(); byteIdx++)
            {
                uint8_t hi = HexCharToNibble(line[byteIdx * 2]);
                uint8_t lo = HexCharToNibble(line[byteIdx * 2 + 1]);
                uint8_t byte = (hi << 4) | lo;

                for (int bit = 7; bit >= 0; bit--)
                {
                    int pixelX = byteIdx * 8 + (7 - bit);
                    if (pixelX >= currentGlyph.bbxW)
                        break;
                    int idx = bitmapRow * currentGlyph.bbxW + pixelX;
                    currentGlyph.bitmap[idx] = (byte & (1 << bit)) ? 255 : 0;
                }
            }
            bitmapRow++;
            continue;
        }

        // Parse keywords
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "FONT_ASCENT")
        {
            iss >> outFont.ascent;
        }
        else if (keyword == "FONT_DESCENT")
        {
            iss >> outFont.descent;
        }
        else if (keyword == "STARTCHAR")
        {
            inChar = true;
            currentGlyph = BdfGlyph();
        }
        else if (keyword == "ENCODING" && inChar)
        {
            iss >> currentGlyph.encoding;
        }
        else if (keyword == "DWIDTH" && inChar)
        {
            iss >> currentGlyph.dwidthX;
        }
        else if (keyword == "BBX" && inChar)
        {
            iss >> currentGlyph.bbxW >> currentGlyph.bbxH >> currentGlyph.bbxOffX >> currentGlyph.bbxOffY;
        }
        else if (keyword == "BITMAP" && inChar)
        {
            inBitmap = true;
            bitmapRow = 0;
            currentGlyph.bitmap.resize(currentGlyph.bbxW * currentGlyph.bbxH, 0);
        }
        else if (keyword == "ENDFONT")
        {
            break;
        }
    }

    return !outFont.glyphs.empty();
}

} // anonymous namespace

std::vector<int> FontCompiler::GetBdfCodepoints(const std::string& bdfPath)
{
    std::vector<int> codepoints;
    BdfFontData bdfData;
    if (!ParseBdfFile(bdfPath, bdfData))
        return codepoints;

    codepoints.reserve(bdfData.glyphs.size());
    for (const auto& g : bdfData.glyphs)
        codepoints.push_back(g.encoding);

    return codepoints;
}

bool FontCompiler::CompileBdfFont(
    const std::string& bdfPath,
    const BdfCompileOptions& options,
    CompileResult& outResult)
{
    if (bdfPath.empty() || !std::filesystem::exists(bdfPath))
        return false;

    if (options.selectedChars.empty())
        return false;

    // Parse BDF file
    BdfFontData bdfData;
    if (!ParseBdfFile(bdfPath, bdfData))
        return false;

    // Build lookup: codepoint -> index in bdfData.glyphs
    std::unordered_map<int, int> codepointToGlyph;
    for (int i = 0; i < (int)bdfData.glyphs.size(); i++)
        codepointToGlyph[bdfData.glyphs[i].encoding] = i;

    // Build sorted set of selected codepoints that exist in the BDF
    std::set<int> selectedSet(options.selectedChars.begin(), options.selectedChars.end());
    if (selectedSet.empty() || *selectedSet.begin() < 0)
        return false;

    int firstChar = *selectedSet.begin();
    int lastChar = *selectedSet.rbegin();
    int padding = options.padding;

    // V2 sparse format: only store glyphs that actually exist in the BDF
    struct PackedGlyph
    {
        int codepoint = 0;
        int width = 0, height = 0;
        int offsetX = 0, offsetY = 0;
        int advance = 0;
        const uint8_t* bitmapData = nullptr;
        bool hasData = false;
    };
    std::vector<PackedGlyph> packedGlyphs;
    packedGlyphs.reserve(selectedSet.size());

    int totalWidth = 0;
    int maxHeight = 0;

    for (int codepoint : selectedSet)
    {
        auto it = codepointToGlyph.find(codepoint);
        if (it == codepointToGlyph.end())
            continue; // Selected but not in BDF - skip entirely

        const BdfGlyph& g = bdfData.glyphs[it->second];
        PackedGlyph pg;
        pg.codepoint = codepoint;
        pg.width = g.bbxW;
        pg.height = g.bbxH;
        pg.offsetX = g.bbxOffX;
        pg.offsetY = -(g.bbxOffY + g.bbxH); // BDF to GlyphInfo convention
        pg.advance = g.dwidthX;
        pg.bitmapData = g.bitmap.data();
        pg.hasData = !g.bitmap.empty() && g.bbxW > 0 && g.bbxH > 0;

        totalWidth += g.bbxW + padding * 2;
        if (g.bbxH > maxHeight)
            maxHeight = g.bbxH;

        packedGlyphs.push_back(pg);
    }

    if (packedGlyphs.empty())
        return false;

    int glyphCount = static_cast<int>(packedGlyphs.size());

    // Sort glyphs by height (tallest first) for tighter shelf packing
    std::vector<int> sortedIndices(glyphCount);
    for (int i = 0; i < glyphCount; i++) sortedIndices[i] = i;
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        return packedGlyphs[a].height > packedGlyphs[b].height;
    });

    // Try multiple atlas widths and pick the one that minimizes total area
    int bestWidth = 64;
    int bestArea = INT_MAX;
    for (int tryWidth = 64; tryWidth <= options.maxAtlasSize; tryWidth *= 2)
    {
        int cx = padding, cy = padding, rh = 0;
        for (int idx : sortedIndices)
        {
            if (!packedGlyphs[idx].hasData) continue;
            int gw = packedGlyphs[idx].width + padding * 2;
            int gh = packedGlyphs[idx].height + padding * 2;
            if (cx + gw > tryWidth) { cx = padding; cy += rh; rh = 0; }
            cx += gw;
            if (gh > rh) rh = gh;
        }
        int h = cy + rh + padding;
        int area = tryWidth * h;
        if (area < bestArea) { bestArea = area; bestWidth = tryWidth; }
    }
    int atlasWidth = bestWidth;

    // Pack glyphs in height-sorted order - sparse: one entry per actual glyph
    outResult.glyphs.resize(glyphCount);
    outResult.codepoints.resize(glyphCount);

    // First pass: set codepoints and defaults for all glyphs
    for (int i = 0; i < glyphCount; i++)
    {
        outResult.codepoints[i] = static_cast<uint32_t>(packedGlyphs[i].codepoint);
        if (!packedGlyphs[i].hasData)
        {
            outResult.glyphs[i] = {};
            outResult.glyphs[i].advance = static_cast<uint8_t>(packedGlyphs[i].advance);
        }
    }

    // Second pass: pack in height-sorted order
    int cursorX = padding;
    int cursorY = padding;
    int rowHeight = 0;

    for (int idx : sortedIndices)
    {
        if (!packedGlyphs[idx].hasData)
            continue;

        int glyphW = packedGlyphs[idx].width + padding * 2;
        int glyphH = packedGlyphs[idx].height + padding * 2;

        if (cursorX + glyphW > atlasWidth)
        {
            cursorX = padding;
            cursorY += rowHeight;
            rowHeight = 0;
        }

        outResult.glyphs[idx].x = static_cast<uint16_t>(cursorX);
        outResult.glyphs[idx].y = static_cast<uint16_t>(cursorY);
        outResult.glyphs[idx].width = static_cast<uint8_t>(packedGlyphs[idx].width);
        outResult.glyphs[idx].height = static_cast<uint8_t>(packedGlyphs[idx].height);
        outResult.glyphs[idx].offset_x = static_cast<int8_t>(packedGlyphs[idx].offsetX);
        outResult.glyphs[idx].offset_y = static_cast<int8_t>(packedGlyphs[idx].offsetY);
        outResult.glyphs[idx].advance = static_cast<uint8_t>(packedGlyphs[idx].advance);

        cursorX += glyphW;
        if (glyphH > rowHeight)
            rowHeight = glyphH;
    }

    // Final atlas height — exact fit, no power-of-2 rounding
    int atlasHeight = (std::max)(cursorY + rowHeight + padding, 1);

    // Create atlas bitmap (RGBA: white + alpha)
    outResult.atlasRGBA.resize(atlasWidth * atlasHeight * 4, 0);

    for (int i = 0; i < glyphCount; i++)
    {
        if (!packedGlyphs[i].hasData)
            continue;

        int destX = outResult.glyphs[i].x;
        int destY = outResult.glyphs[i].y;

        for (int y = 0; y < packedGlyphs[i].height; y++)
        {
            for (int x = 0; x < packedGlyphs[i].width; x++)
            {
                int srcIdx = y * packedGlyphs[i].width + x;
                int dstIdx = ((destY + y) * atlasWidth + (destX + x)) * 4;
                uint8_t alpha = packedGlyphs[i].bitmapData[srcIdx];
                outResult.atlasRGBA[dstIdx + 0] = 255;   // R
                outResult.atlasRGBA[dstIdx + 1] = 255;   // G
                outResult.atlasRGBA[dstIdx + 2] = 255;   // B
                outResult.atlasRGBA[dstIdx + 3] = alpha;  // A
            }
        }
    }

    // Set result metadata
    outResult.atlasWidth = static_cast<uint32_t>(atlasWidth);
    outResult.atlasHeight = static_cast<uint32_t>(atlasHeight);
    outResult.firstChar = static_cast<uint32_t>(firstChar);
    outResult.lastChar = static_cast<uint32_t>(lastChar);
    outResult.lineHeight = static_cast<uint8_t>(bdfData.ascent + bdfData.descent);
    outResult.baseline = static_cast<uint8_t>(bdfData.ascent);
    outResult.isSparse = true;

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

    if (result.isSparse && !result.codepoints.empty())
    {
        // V2: sparse codepoint table format
        FontHeaderV2 header;
        memcpy(header.magic, "DFNT", 4);
        header.version = 2;
        header.first_codepoint = result.firstChar;
        header.last_codepoint = result.lastChar;
        header.line_height = result.lineHeight;
        header.baseline = result.baseline;
        header.glyph_count = static_cast<uint16_t>(result.glyphs.size());
        header.atlas_path_len = static_cast<uint16_t>(atlasFilename.length() + 1);
        header.reserved = 0;

        std::ofstream fontFile(path, std::ios::binary);
        if (!fontFile)
            return false;

        fontFile.write(reinterpret_cast<const char*>(&header), sizeof(FontHeaderV2));
        fontFile.write(reinterpret_cast<const char*>(result.codepoints.data()),
                       result.codepoints.size() * sizeof(uint32_t));
        fontFile.write(reinterpret_cast<const char*>(result.glyphs.data()),
                       result.glyphs.size() * sizeof(GlyphInfo));
        fontFile.write(atlasFilename.c_str(), atlasFilename.length() + 1);
        fontFile.close();
        return true;
    }

    // V1: contiguous ASCII format
    FontHeader header;
    memcpy(header.magic, "DFNT", 4);
    header.version = 1;
    header.first_char = static_cast<uint8_t>(result.firstChar);
    header.last_char = static_cast<uint8_t>(result.lastChar);
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
