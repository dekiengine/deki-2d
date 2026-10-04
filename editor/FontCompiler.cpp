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

// Dilation with a square kernel: each dst pixel is the max over a
// (2N+1)x(2N+1) neighbourhood. src is srcW x srcH; dst must be
// (srcW + 2*N) x (srcH + 2*N), with the source glyph at offset (N, N).
static void DilateAlphaSquare(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int n)
{
    const int dstW = srcW + 2 * n;
    const int dstH = srcH + 2 * n;
    for (int dy = 0; dy < dstH; ++dy)
    {
        for (int dx = 0; dx < dstW; ++dx)
        {
            // dst (dx, dy) is source (dx - N, dy - N).
            uint8_t maxA = 0;
            for (int ky = -n; ky <= n; ++ky)
            {
                int sy = (dy - n) + ky;
                if (sy < 0 || sy >= srcH)
                {
                    continue;
                }
                for (int kx = -n; kx <= n; ++kx)
                {
                    int sx = (dx - n) + kx;
                    if (sx < 0 || sx >= srcW)
                    {
                        continue;
                    }
                    uint8_t v = src[sy * srcW + sx];
                    if (v > maxA)
                    {
                        maxA = v;
                    }
                }
            }
            dst[dy * dstW + dx] = maxA;
        }
    }
}

// Copies src into the zero-cleared dst at offset (ox, oy). Puts the fill and
// the shadow on the same expanded canvas.
static void CopyAlphaAt(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH, int ox, int oy)
{
    for (int y = 0; y < srcH; ++y)
    {
        int dy = y + oy;
        if (dy < 0 || dy >= dstH)
        {
            continue;
        }
        for (int x = 0; x < srcW; ++x)
        {
            int dx = x + ox;
            if (dx < 0 || dx >= dstW)
            {
                continue;
            }
            dst[dy * dstW + dx] = src[y * srcW + x];
        }
    }
}

// Maps a (decoration alpha, fill alpha) pair to a 4-bit palette index 0..15.
// Callers put it in the low nibble of an atlas byte; the high nibble stays 0.
static uint8_t ClassifyToPaletteIndex(uint8_t decoA, uint8_t fillA)
{
    if (fillA > 0)
    {
        // 5 + round(fillA / 255 * 10) -> 5..15
        int bucket = 5 + (fillA * 10 + 127) / 255;
        if (bucket < 5)
        {
            bucket = 5;
        }
        if (bucket > 15)
        {
            bucket = 15;
        }
        return static_cast<uint8_t>(bucket);
    }
    if (decoA > 0)
    {
        // 1 + (decoA * 4 / 255) clamped to 1..4
        int bucket = 1 + (decoA * 4) / 255;
        if (bucket < 1)
        {
            bucket = 1;
        }
        if (bucket > 4)
        {
            bucket = 4;
        }
        return static_cast<uint8_t>(bucket);
    }
    return 0;
}

// Box-filter downsample of an 8-bit grayscale bitmap by N on each axis.
// The source size need not divide by N (dst size = ceil(src/N)); see below
// for the edge cells.
static void BoxDownsample(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH, int n)
{
    const int denom = n * n;
    const int half = denom / 2;
    for (int dy = 0; dy < dstH; ++dy)
    {
        for (int dx = 0; dx < dstW; ++dx)
        {
            int sum = 0;
            int covered = 0;
            int sy0 = dy * n;
            int sx0 = dx * n;
            for (int ky = 0; ky < n; ++ky)
            {
                int sy = sy0 + ky;
                if (sy >= srcH)
                {
                    break;
                }
                for (int kx = 0; kx < n; ++kx)
                {
                    int sx = sx0 + kx;
                    if (sx >= srcW)
                    {
                        break;
                    }
                    sum += src[sy * srcW + sx];
                    ++covered;
                }
            }
            // Divide by the full NxN window, so edge cells fade out: missing
            // samples count as 0, which is what a transparent background is.
            (void)covered;
            dst[dy * dstW + dx] = static_cast<uint8_t>((sum + half) / denom);
        }
    }
}

bool FontCompiler::CompileTrueTypeFont(const std::string& ttfPath, const CompileOptions& options,
                                       CompileResult& outResult)
{
    if (ttfPath.empty() || !std::filesystem::exists(ttfPath))
    {
        return false;
    }

    FT_Library ft;
    if (FT_Init_FreeType(&ft))
    {
        return false;
    }

    FT_Face face;
    if (FT_New_Face(ft, ttfPath.c_str(), 0, &face))
    {
        FT_Done_FreeType(ft);
        return false;
    }

    // Font-wide metrics at the TARGET size: baseline and lineHeight are stored
    // in target pixels whatever the oversampling.
    FT_Set_Pixel_Sizes(face, 0, options.fontSize);
    int ascender = static_cast<int>(face->size->metrics.ascender >> 6);     // Max height above baseline
    int descender = -static_cast<int>(face->size->metrics.descender >> 6);  // Max depth below baseline (make positive)
    int cellHeight = ascender + descender;                                  // Uniform height for all glyphs
    int baseline = ascender;                                                // Baseline position from top of cell

    FT_Int32 loadFlags = FT_LOAD_RENDER;
    switch (options.hinting)
    {
        case HintingMode::None: loadFlags |= FT_LOAD_NO_HINTING; break;
        case HintingMode::Light: loadFlags |= FT_LOAD_TARGET_LIGHT; break;
        case HintingMode::Normal: loadFlags |= FT_LOAD_TARGET_NORMAL; break;
        case HintingMode::Mono: loadFlags |= FT_LOAD_TARGET_MONO; break;
    }
    const bool isMono = (options.hinting == HintingMode::Mono);

    // Mono is 1-bit, so oversampling does not help it.
    int oversampleN = options.oversample;
    if (oversampleN < 1)
    {
        oversampleN = 1;
    }
    if (oversampleN > 4)
    {
        oversampleN = 4;
    }
    if (isMono)
    {
        oversampleN = 1;
    }

    // Decoration settings. The per-glyph canvas padding is worked out first,
    // so the glyph loop can map pixels to palette indices.
    DecorationMode decoration = options.decoration;
    int outlineSize = options.outlineSize;
    int shadowDx = options.shadowDx;
    int shadowDy = options.shadowDy;
    if (outlineSize < 1)
    {
        outlineSize = 1;
    }
    if (outlineSize > 3)
    {
        outlineSize = 3;
    }
    if (shadowDx < -3)
    {
        shadowDx = -3;
    }
    if (shadowDx > 3)
    {
        shadowDx = 3;
    }
    if (shadowDy < -3)
    {
        shadowDy = -3;
    }
    if (shadowDy > 3)
    {
        shadowDy = 3;
    }
    // A shadow at (0, 0) would match the fill, so it is no decoration.
    if (decoration == DecorationMode::Shadow && shadowDx == 0 && shadowDy == 0)
    {
        decoration = DecorationMode::None;
    }

    int padL = 0, padR = 0, padT = 0, padB = 0;
    if (decoration == DecorationMode::Outline)
    {
        padL = padR = padT = padB = outlineSize;
    }
    else if (decoration == DecorationMode::Shadow)
    {
        padL = (shadowDx < 0) ? -shadowDx : 0;
        padR = (shadowDx > 0) ? shadowDx : 0;
        padT = (shadowDy < 0) ? -shadowDy : 0;
        padB = (shadowDy > 0) ? shadowDy : 0;
    }
    const bool decorate = (decoration != DecorationMode::None);

    // Glyphs load at the oversampled size. The cap/x-height probe below uses
    // the same size, so its metrics match the glyphs'.
    if (oversampleN > 1)
    {
        FT_Set_Pixel_Sizes(face, 0, options.fontSize * oversampleN);
    }

    // Cap height and x-height come from 'H' and 'x'. bitmap_top is in
    // oversampled pixels; dividing gives target pixels.
    int capHeight = 0;
    int xHeight = 0;
    if (FT_Load_Char(face, 'H', loadFlags) == 0)
    {
        capHeight = face->glyph->bitmap_top / oversampleN;
    }
    if (FT_Load_Char(face, 'x', loadFlags) == 0)
    {
        xHeight = face->glyph->bitmap_top / oversampleN;
    }

    const int firstChar = options.firstChar;
    const int lastChar = options.lastChar;
    const int charCount = lastChar - firstChar + 1;
    const int padding = options.padding;

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
        if (FT_Load_Char(face, c, loadFlags))
        {
            glyphData[i].width = 0;
            glyphData[i].height = 0;
            glyphData[i].bearingX = 0;
            glyphData[i].bearingY = 0;
            glyphData[i].advance = options.fontSize / 2;
            continue;
        }

        FT_GlyphSlot g = face->glyph;

        // Oversampled size (the target size when oversampleN == 1).
        const int srcW = static_cast<int>(g->bitmap.width);
        const int srcH = static_cast<int>(g->bitmap.rows);

        // Target size and bearings, rounded up so no edge pixel is lost.
        const int dstW = (srcW + oversampleN - 1) / oversampleN;
        const int dstH = (srcH + oversampleN - 1) / oversampleN;
        glyphData[i].width = dstW;
        glyphData[i].height = dstH;
        glyphData[i].bearingX = g->bitmap_left / oversampleN;
        glyphData[i].bearingY = g->bitmap_top / oversampleN;
        // Advance is 26.6 fixed point at the OVERSAMPLED scale. One division
        // turns it into whole target pixels, rounded to nearest.
        glyphData[i].advance = static_cast<int>((g->advance.x + 32 * oversampleN) / (64 * oversampleN));

        if (g->bitmap.buffer && srcW > 0 && srcH > 0)
        {
            if (isMono)
            {
                // Mono, so oversampleN == 1. 1-bit bitmap, MSB first, rows padded.
                glyphData[i].bitmap.resize(dstW * dstH);
                for (int row = 0; row < srcH; row++)
                {
                    const uint8_t* srcRow = g->bitmap.buffer + row * g->bitmap.pitch;
                    uint8_t* dstRow = glyphData[i].bitmap.data() + row * dstW;
                    for (int col = 0; col < srcW; col++)
                    {
                        uint8_t byte = srcRow[col >> 3];
                        uint8_t bit = 0x80 >> (col & 7);
                        dstRow[col] = (byte & bit) ? 255 : 0;
                    }
                }
            }
            else if (oversampleN == 1)
            {
                // No oversampling: a straight copy, keeping FreeType's row pitch.
                glyphData[i].bitmap.resize(dstW * dstH);
                for (int row = 0; row < srcH; row++)
                {
                    memcpy(glyphData[i].bitmap.data() + row * dstW, g->bitmap.buffer + row * g->bitmap.pitch, srcW);
                }
            }
            else
            {
                // Copy the oversampled bitmap (minding its pitch) into a
                // contiguous buffer, then box-filter it down to the target size.
                std::vector<uint8_t> src(srcW * srcH);
                for (int row = 0; row < srcH; row++)
                {
                    memcpy(src.data() + row * srcW, g->bitmap.buffer + row * g->bitmap.pitch, srcW);
                }
                glyphData[i].bitmap.resize(dstW * dstH);
                BoxDownsample(src.data(), srcW, srcH, glyphData[i].bitmap.data(), dstW, dstH, oversampleN);
            }
        }

        // Decoration: grow the canvas, make the outline or shadow shape, and
        // map each pixel to a 4-bit palette index in the low nibble. The atlas
        // is still 8 bits per pixel on disk; the font header's m_DecorationMode
        // tells the runtime to read the bytes as palette indices, not alpha.
        if (decorate && !glyphData[i].bitmap.empty())
        {
            const int fillW = glyphData[i].width;
            const int fillH = glyphData[i].height;
            const int expW = fillW + padL + padR;
            const int expH = fillH + padT + padB;

            std::vector<uint8_t> fillExp(expW * expH, 0);
            CopyAlphaAt(glyphData[i].bitmap.data(), fillW, fillH, fillExp.data(), expW, expH, padL, padT);

            std::vector<uint8_t> decoExp(expW * expH, 0);
            if (decoration == DecorationMode::Outline)
            {
                DilateAlphaSquare(glyphData[i].bitmap.data(), fillW, fillH, decoExp.data(), outlineSize);
            }
            else  // Shadow
            {
                CopyAlphaAt(glyphData[i].bitmap.data(), fillW, fillH, decoExp.data(), expW, expH, padL + shadowDx,
                            padT + shadowDy);
            }

            std::vector<uint8_t> indexed(expW * expH);
            for (int p = 0; p < expW * expH; ++p)
            {
                indexed[p] = ClassifyToPaletteIndex(decoExp[p], fillExp[p]);
            }

            glyphData[i].bitmap = std::move(indexed);
            glyphData[i].width = expW;
            glyphData[i].height = expH;
            glyphData[i].bearingX -= padL;
            glyphData[i].bearingY += padT;
        }

        totalWidth += glyphData[i].width + padding * 2;
        if (glyphData[i].height > maxHeight)
        {
            maxHeight = glyphData[i].height;
        }
    }

    // Tallest first packs the shelves tighter.
    std::vector<int> sortedIndices(charCount);
    for (int i = 0; i < charCount; i++)
    {
        sortedIndices[i] = i;
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(),
              [&](int a, int b) { return glyphData[a].height > glyphData[b].height; });

    // Try several atlas widths and keep the one with the smallest area.
    int bestWidth = 64;
    int bestArea = INT_MAX;
    for (int tryWidth = 64; tryWidth <= options.maxAtlasSize; tryWidth *= 2)
    {
        int cx = padding, cy = padding, rh = 0;
        for (int idx : sortedIndices)
        {
            int gw = glyphData[idx].width + padding * 2;
            int gh = glyphData[idx].height + padding * 2;
            if (cx + gw > tryWidth)
            {
                cx = padding;
                cy += rh;
                rh = 0;
            }
            cx += gw;
            if (gh > rh)
            {
                rh = gh;
            }
        }
        int h = cy + rh + padding;
        int area = tryWidth * h;
        if (area < bestArea)
        {
            bestArea = area;
            bestWidth = tryWidth;
        }
    }
    int atlasWidth = bestWidth;

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
        outResult.glyphs[idx].offsetX = static_cast<int8_t>(glyphData[idx].bearingX);
        // offsetY = -bearingY: negative means the glyph rises above the baseline.
        outResult.glyphs[idx].offsetY = static_cast<int8_t>(-glyphData[idx].bearingY);
        outResult.glyphs[idx].advance = static_cast<uint8_t>(glyphData[idx].advance);

        cursorX += glyphW;
        if (glyphH > rowHeight)
        {
            rowHeight = glyphH;
        }
    }

    // Exact fit, not rounded to a power of 2.
    int atlasHeight = cursorY + rowHeight + padding;

    outResult.atlasRGBA.resize(atlasWidth * atlasHeight * 4, 0);

    for (int i = 0; i < charCount; i++)
    {
        if (glyphData[i].bitmap.empty())
        {
            continue;
        }

        int destX = outResult.glyphs[i].x;
        int destY = outResult.glyphs[i].y;

        for (int y = 0; y < glyphData[i].height; y++)
        {
            for (int x = 0; x < glyphData[i].width; x++)
            {
                int srcIdx = y * glyphData[i].width + x;
                int dstIdx = ((destY + y) * atlasWidth + (destX + x)) * 4;
                uint8_t alpha = glyphData[i].bitmap[srcIdx];
                outResult.atlasRGBA[dstIdx + 0] = 255;    // R
                outResult.atlasRGBA[dstIdx + 1] = 255;    // G
                outResult.atlasRGBA[dstIdx + 2] = 255;    // B
                outResult.atlasRGBA[dstIdx + 3] = alpha;  // A
            }
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    outResult.atlasWidth = static_cast<uint32_t>(atlasWidth);
    outResult.atlasHeight = static_cast<uint32_t>(atlasHeight);
    outResult.firstChar = static_cast<uint8_t>(firstChar);
    outResult.lastChar = static_cast<uint8_t>(lastChar);
    outResult.lineHeight = static_cast<uint8_t>(cellHeight);
    outResult.baseline = static_cast<uint8_t>(baseline);
    // Without 'H' or 'x', use typical ratios.
    if (capHeight <= 0)
    {
        capHeight = (ascender * 7) / 10;
    }
    if (xHeight <= 0)
    {
        xHeight = (ascender * 5) / 10;
    }
    outResult.capHeight = static_cast<uint8_t>((std::min)(capHeight, 255));
    outResult.xHeight = static_cast<uint8_t>((std::min)(xHeight, 255));

    outResult.decoration = decoration;
    if (decoration == DecorationMode::Outline)
    {
        outResult.decorationA = static_cast<int8_t>(outlineSize);
        outResult.decorationB = 0;
    }
    else if (decoration == DecorationMode::Shadow)
    {
        outResult.decorationA = static_cast<int8_t>(shadowDx);
        outResult.decorationB = static_cast<int8_t>(shadowDy);
    }
    else
    {
        outResult.decorationA = 0;
        outResult.decorationB = 0;
    }

    return true;
}

bool FontCompiler::GenerateMonospaceGlyphs(int glyphWidth, int glyphHeight, int firstChar, int charCount,
                                           int charsPerRow, std::vector<GlyphInfo>& outGlyphs)
{
    if (glyphWidth <= 0 || glyphHeight <= 0 || charCount <= 0 || charsPerRow <= 0)
    {
        return false;
    }

    outGlyphs.resize(charCount);

    for (int i = 0; i < charCount; i++)
    {
        int col = i % charsPerRow;
        int row = i / charsPerRow;

        outGlyphs[i].x = static_cast<uint16_t>(col * glyphWidth);
        outGlyphs[i].y = static_cast<uint16_t>(row * glyphHeight);
        outGlyphs[i].width = static_cast<uint8_t>(glyphWidth);
        outGlyphs[i].height = static_cast<uint8_t>(glyphHeight);
        outGlyphs[i].offsetX = 0;
        outGlyphs[i].offsetY = 0;
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
    std::vector<uint8_t> bitmap;  // one byte per pixel, 0 or 255
};

struct BdfFontData
{
    int ascent = 0;
    int descent = 0;
    std::vector<BdfGlyph> glyphs;
};

static uint8_t HexCharToNibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'A' && c <= 'F')
    {
        return static_cast<uint8_t>(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f')
    {
        return static_cast<uint8_t>(c - 'a' + 10);
    }
    return 0;
}

static bool ParseBdfFile(const std::string& path, BdfFontData& outFont)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    std::string line;
    BdfGlyph currentGlyph;
    bool inChar = false;
    bool inBitmap = false;
    int bitmapRow = 0;

    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty())
        {
            continue;
        }

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

            // One hex row of the bitmap.
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
                    {
                        break;
                    }
                    int idx = bitmapRow * currentGlyph.bbxW + pixelX;
                    currentGlyph.bitmap[idx] = (byte & (1 << bit)) ? 255 : 0;
                }
            }
            bitmapRow++;
            continue;
        }

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

}  // anonymous namespace

std::vector<int> FontCompiler::GetBdfCodepoints(const std::string& bdfPath)
{
    std::vector<int> codepoints;
    BdfFontData bdfData;
    if (!ParseBdfFile(bdfPath, bdfData))
    {
        return codepoints;
    }

    codepoints.reserve(bdfData.glyphs.size());
    for (const auto& g : bdfData.glyphs)
    {
        codepoints.push_back(g.encoding);
    }

    return codepoints;
}

bool FontCompiler::CompileBdfFont(const std::string& bdfPath, const BdfCompileOptions& options,
                                  CompileResult& outResult)
{
    if (bdfPath.empty() || !std::filesystem::exists(bdfPath))
    {
        return false;
    }

    if (options.selectedChars.empty())
    {
        return false;
    }

    BdfFontData bdfData;
    if (!ParseBdfFile(bdfPath, bdfData))
    {
        return false;
    }

    // Codepoint to index in bdfData.glyphs.
    std::unordered_map<int, int> codepointToGlyph;
    for (int i = 0; i < (int)bdfData.glyphs.size(); i++)
    {
        codepointToGlyph[bdfData.glyphs[i].encoding] = i;
    }

    // The selected codepoints, sorted.
    std::set<int> selectedSet(options.selectedChars.begin(), options.selectedChars.end());
    if (selectedSet.empty() || *selectedSet.begin() < 0)
    {
        return false;
    }

    int firstChar = *selectedSet.begin();
    int lastChar = *selectedSet.rbegin();
    int padding = options.padding;

    // Decoration settings, handled as in the TTF path: clamp, drop a shadow
    // at (0, 0), and work out the padding per side.
    DecorationMode bdfDecoration = options.decoration;
    int bdfOutlineSize = options.outlineSize;
    int bdfShadowDx = options.shadowDx;
    int bdfShadowDy = options.shadowDy;
    if (bdfOutlineSize < 1)
    {
        bdfOutlineSize = 1;
    }
    if (bdfOutlineSize > 3)
    {
        bdfOutlineSize = 3;
    }
    if (bdfShadowDx < -3)
    {
        bdfShadowDx = -3;
    }
    if (bdfShadowDx > 3)
    {
        bdfShadowDx = 3;
    }
    if (bdfShadowDy < -3)
    {
        bdfShadowDy = -3;
    }
    if (bdfShadowDy > 3)
    {
        bdfShadowDy = 3;
    }
    if (bdfDecoration == DecorationMode::Shadow && bdfShadowDx == 0 && bdfShadowDy == 0)
    {
        bdfDecoration = DecorationMode::None;
    }

    int bdfPadL = 0, bdfPadR = 0, bdfPadT = 0, bdfPadB = 0;
    if (bdfDecoration == DecorationMode::Outline)
    {
        bdfPadL = bdfPadR = bdfPadT = bdfPadB = bdfOutlineSize;
    }
    else if (bdfDecoration == DecorationMode::Shadow)
    {
        bdfPadL = (bdfShadowDx < 0) ? -bdfShadowDx : 0;
        bdfPadR = (bdfShadowDx > 0) ? bdfShadowDx : 0;
        bdfPadT = (bdfShadowDy < 0) ? -bdfShadowDy : 0;
        bdfPadB = (bdfShadowDy > 0) ? bdfShadowDy : 0;
    }
    const bool bdfDecorate = (bdfDecoration != DecorationMode::None);

    // v2 sparse format: only the glyphs the BDF has are stored.
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
        {
            continue;  // selected but not in the BDF
        }

        const BdfGlyph& g = bdfData.glyphs[it->second];
        PackedGlyph pg;
        pg.codepoint = codepoint;
        pg.width = g.bbxW;
        pg.height = g.bbxH;
        pg.offsetX = g.bbxOffX;
        pg.offsetY = -(g.bbxOffY + g.bbxH);  // BDF to GlyphInfo convention
        pg.advance = g.dwidthX;
        pg.bitmapData = g.bitmap.data();
        pg.hasData = !g.bitmap.empty() && g.bbxW > 0 && g.bbxH > 0;

        totalWidth += g.bbxW + padding * 2;
        if (g.bbxH > maxHeight)
        {
            maxHeight = g.bbxH;
        }

        packedGlyphs.push_back(pg);
    }

    if (packedGlyphs.empty())
    {
        return false;
    }

    // Storage for decorated bitmaps. PackedGlyph::bitmapData otherwise points
    // into BdfGlyph::bitmap, which must not change, so a decorated glyph gets
    // its own indexed bitmap on the expanded canvas and bitmapData points there.
    std::vector<std::vector<uint8_t>> decoratedOwned;
    if (bdfDecorate)
    {
        decoratedOwned.resize(packedGlyphs.size());
        for (size_t i = 0; i < packedGlyphs.size(); ++i)
        {
            PackedGlyph& pg = packedGlyphs[i];
            if (!pg.hasData)
            {
                continue;
            }

            const int fillW = pg.width;
            const int fillH = pg.height;
            const int expW = fillW + bdfPadL + bdfPadR;
            const int expH = fillH + bdfPadT + bdfPadB;

            std::vector<uint8_t> fillExp(expW * expH, 0);
            CopyAlphaAt(pg.bitmapData, fillW, fillH, fillExp.data(), expW, expH, bdfPadL, bdfPadT);

            std::vector<uint8_t> decoExp(expW * expH, 0);
            if (bdfDecoration == DecorationMode::Outline)
            {
                DilateAlphaSquare(pg.bitmapData, fillW, fillH, decoExp.data(), bdfOutlineSize);
            }
            else  // Shadow
            {
                CopyAlphaAt(pg.bitmapData, fillW, fillH, decoExp.data(), expW, expH, bdfPadL + bdfShadowDx,
                            bdfPadT + bdfShadowDy);
            }

            std::vector<uint8_t>& indexed = decoratedOwned[i];
            indexed.resize(expW * expH);
            for (int p = 0; p < expW * expH; ++p)
            {
                indexed[p] = ClassifyToPaletteIndex(decoExp[p], fillExp[p]);
            }

            pg.width = expW;
            pg.height = expH;
            pg.offsetX -= bdfPadL;
            pg.offsetY -= bdfPadT;  // offsetY is -(bbxOffY + bbxH), so padding above the glyph makes it
                                    // more negative.
            pg.bitmapData = indexed.data();
        }
    }

    int glyphCount = static_cast<int>(packedGlyphs.size());

    // Tallest first packs the shelves tighter.
    std::vector<int> sortedIndices(glyphCount);
    for (int i = 0; i < glyphCount; i++)
    {
        sortedIndices[i] = i;
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(),
              [&](int a, int b) { return packedGlyphs[a].height > packedGlyphs[b].height; });

    // Try several atlas widths and keep the one with the smallest area.
    int bestWidth = 64;
    int bestArea = INT_MAX;
    for (int tryWidth = 64; tryWidth <= options.maxAtlasSize; tryWidth *= 2)
    {
        int cx = padding, cy = padding, rh = 0;
        for (int idx : sortedIndices)
        {
            if (!packedGlyphs[idx].hasData)
            {
                continue;
            }
            int gw = packedGlyphs[idx].width + padding * 2;
            int gh = packedGlyphs[idx].height + padding * 2;
            if (cx + gw > tryWidth)
            {
                cx = padding;
                cy += rh;
                rh = 0;
            }
            cx += gw;
            if (gh > rh)
            {
                rh = gh;
            }
        }
        int h = cy + rh + padding;
        int area = tryWidth * h;
        if (area < bestArea)
        {
            bestArea = area;
            bestWidth = tryWidth;
        }
    }
    int atlasWidth = bestWidth;

    // Sparse: one entry per glyph the font has.
    outResult.glyphs.resize(glyphCount);
    outResult.codepoints.resize(glyphCount);

    // First pass: codepoints and defaults for every glyph.
    for (int i = 0; i < glyphCount; i++)
    {
        outResult.codepoints[i] = static_cast<uint32_t>(packedGlyphs[i].codepoint);
        if (!packedGlyphs[i].hasData)
        {
            outResult.glyphs[i] = {};
            outResult.glyphs[i].advance = static_cast<uint8_t>(packedGlyphs[i].advance);
        }
    }

    // Second pass: pack, tallest first.
    int cursorX = padding;
    int cursorY = padding;
    int rowHeight = 0;

    for (int idx : sortedIndices)
    {
        if (!packedGlyphs[idx].hasData)
        {
            continue;
        }

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
        outResult.glyphs[idx].offsetX = static_cast<int8_t>(packedGlyphs[idx].offsetX);
        outResult.glyphs[idx].offsetY = static_cast<int8_t>(packedGlyphs[idx].offsetY);
        outResult.glyphs[idx].advance = static_cast<uint8_t>(packedGlyphs[idx].advance);

        cursorX += glyphW;
        if (glyphH > rowHeight)
        {
            rowHeight = glyphH;
        }
    }

    // Exact fit, not rounded to a power of 2.
    int atlasHeight = (std::max)(cursorY + rowHeight + padding, 1);

    // White, with the glyph in alpha.
    outResult.atlasRGBA.resize(atlasWidth * atlasHeight * 4, 0);

    for (int i = 0; i < glyphCount; i++)
    {
        if (!packedGlyphs[i].hasData)
        {
            continue;
        }

        int destX = outResult.glyphs[i].x;
        int destY = outResult.glyphs[i].y;

        for (int y = 0; y < packedGlyphs[i].height; y++)
        {
            for (int x = 0; x < packedGlyphs[i].width; x++)
            {
                int srcIdx = y * packedGlyphs[i].width + x;
                int dstIdx = ((destY + y) * atlasWidth + (destX + x)) * 4;
                uint8_t alpha = packedGlyphs[i].bitmapData[srcIdx];
                outResult.atlasRGBA[dstIdx + 0] = 255;    // R
                outResult.atlasRGBA[dstIdx + 1] = 255;    // G
                outResult.atlasRGBA[dstIdx + 2] = 255;    // B
                outResult.atlasRGBA[dstIdx + 3] = alpha;  // A
            }
        }
    }

    outResult.atlasWidth = static_cast<uint32_t>(atlasWidth);
    outResult.atlasHeight = static_cast<uint32_t>(atlasHeight);
    outResult.firstChar = static_cast<uint32_t>(firstChar);
    outResult.lastChar = static_cast<uint32_t>(lastChar);
    outResult.lineHeight = static_cast<uint8_t>(bdfData.ascent + bdfData.descent);
    outResult.baseline = static_cast<uint8_t>(bdfData.ascent);
    outResult.isSparse = true;

    outResult.decoration = bdfDecoration;
    if (bdfDecoration == DecorationMode::Outline)
    {
        outResult.decorationA = static_cast<int8_t>(bdfOutlineSize);
        outResult.decorationB = 0;
    }
    else if (bdfDecoration == DecorationMode::Shadow)
    {
        outResult.decorationA = static_cast<int8_t>(bdfShadowDx);
        outResult.decorationB = static_cast<int8_t>(bdfShadowDy);
    }
    else
    {
        outResult.decorationA = 0;
        outResult.decorationB = 0;
    }

    return true;
}

bool FontCompiler::WriteDfontFile(const std::string& path, const CompileResult& result,
                                  const std::string& atlasFilename)
{
    if (path.empty() || result.glyphs.empty())
    {
        return false;
    }

    std::filesystem::path filePath(path);
    std::filesystem::create_directories(filePath.parent_path());

    // v4 header, with the decoration settings. The sparse flag is the high bit
    // of `version` (0x80000004), as in v3.
    FontHeaderV4 header;
    memcpy(header.magic, "DFNT", 4);
    const bool sparse = result.isSparse && !result.codepoints.empty();
    header.version = sparse ? 0x80000004u : 0x00000004u;
    header.firstCodepoint = result.firstChar;
    header.lastCodepoint = result.lastChar;
    header.lineHeight = result.lineHeight;
    header.baseline = result.baseline;
    header.capHeight = result.capHeight;
    header.xHeight = result.xHeight;
    header.decorationMode = static_cast<uint8_t>(result.decoration);
    header.decorationA = result.decorationA;
    header.decorationB = result.decorationB;
    header.reserved = 0;
    header.glyphCount = static_cast<uint16_t>(result.glyphs.size());
    header.atlasPathLen = static_cast<uint16_t>(atlasFilename.length() + 1);

    std::ofstream fontFile(path, std::ios::binary);
    if (!fontFile)
    {
        return false;
    }

    fontFile.write(reinterpret_cast<const char*>(&header), sizeof(FontHeaderV4));
    if (sparse)
    {
        fontFile.write(reinterpret_cast<const char*>(result.codepoints.data()),
                       result.codepoints.size() * sizeof(uint32_t));
    }
    fontFile.write(reinterpret_cast<const char*>(result.glyphs.data()), result.glyphs.size() * sizeof(GlyphInfo));
    fontFile.write(atlasFilename.c_str(), atlasFilename.length() + 1);
    fontFile.close();

    return true;
}

}  // namespace Deki2D

#endif  // DEKI_EDITOR
