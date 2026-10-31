// Where an oversampled glyph lands.
//
// A TrueType glyph is drawn at twice its size and filtered down. Filtered from
// its own corner, a glyph whose oversampled bitmap started on an odd column or
// row came out half a pixel off, so a word's letters looked uneven. Each
// baked glyph's ink must sit where FreeType puts it.

#include <gtest/gtest.h>

#include "editor/FontCompiler.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cmath>
#include <cstdint>
#include <string>

// The package's types are in its namespace; tests name them unqualified.
using namespace Deki2D;

namespace
{

struct Centre
{
    double x = 0.0;
    double y = 0.0;
    bool inked = false;
};

// The ink's centre of an 8-bit coverage bitmap, in its own pixels.
template <typename Coverage>
Centre InkCentre(int width, int height, Coverage coverage)
{
    double sum = 0.0, sx = 0.0, sy = 0.0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const double a = coverage(x, y);
            sum += a;
            sx += (x + 0.5) * a;
            sy += (y + 0.5) * a;
        }
    }
    Centre c;
    if (sum > 0.0)
    {
        c.x = sx / sum;
        c.y = sy / sum;
        c.inked = true;
    }
    return c;
}

void ExpectGlyphsInPlace(int fontSize)
{
    FontCompiler::CompileOptions options;
    options.fontSize = fontSize;
    options.oversample = 2;
    FontCompiler::CompileResult result;
    ASSERT_TRUE(FontCompiler::CompileTrueTypeFont(DEKI_TEST_FONT, options, result));

    // What FreeType draws at the oversampled size, with the same hinting.
    FT_Library ft;
    ASSERT_EQ(FT_Init_FreeType(&ft), 0);
    FT_Face face;
    ASSERT_EQ(FT_New_Face(ft, DEKI_TEST_FONT, 0, &face), 0);
    FT_Set_Pixel_Sizes(face, 0, fontSize * 2);

    for (int c = options.firstChar; c <= options.lastChar; ++c)
    {
        const Deki2D::GlyphInfo& g = result.glyphs[c - options.firstChar];
        const Centre baked = InkCentre(g.width, g.height,
                                       [&](int x, int y)
                                       {
                                           const size_t i = ((g.y + y) * result.atlasWidth + g.x + x) * 4;
                                           return static_cast<double>(result.atlasRGBA[i + 3]);
                                       });

        ASSERT_EQ(FT_Load_Char(face, c, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT), 0);
        const FT_GlyphSlot slot = face->glyph;
        const Centre drawn =
            InkCentre(slot->bitmap.width, slot->bitmap.rows, [&](int x, int y)
                      { return static_cast<double>(slot->bitmap.buffer[y * slot->bitmap.pitch + x]); });
        if (!baked.inked || !drawn.inked)
        {
            continue;
        }

        // From the pen on the baseline, in target pixels; y grows down.
        const double bakedX = g.offsetX + baked.x;
        const double bakedY = g.offsetY + baked.y;
        const double drawnX = (slot->bitmap_left + drawn.x) / 2.0;
        const double drawnY = (-slot->bitmap_top + drawn.y) / 2.0;

        // Filtering 2x2 cells moves the ink by at most a quarter pixel.
        EXPECT_NEAR(bakedX, drawnX, 0.3) << "'" << static_cast<char>(c) << "' at " << fontSize << " px";
        EXPECT_NEAR(bakedY, drawnY, 0.3) << "'" << static_cast<char>(c) << "' at " << fontSize << " px";
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

}  // namespace

TEST(FontCompiler, OversampledGlyphsSitWhereFreeTypePutsThem)
{
    for (int size : { 10, 11, 12, 13, 16 })
    {
        ExpectGlyphsInPlace(size);
    }
}
