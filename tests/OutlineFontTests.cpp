// Fonts kept as outlines, drawn at any size on the device.
//
// The editor turns a TTF into glyph outlines (.dvec); the device draws each
// glyph the first time a text at some size needs it. Drawn without hinting,
// a glyph must match what FreeType draws without hinting; sized, a font must
// measure as a baked one of that size does; and a text must draw at any size
// set on it.

#include <gtest/gtest.h>

#include "BitmapFont.h"
#include "OutlineFont.h"
#include "TextComponent.h"
#include "editor/FontCompiler.h"

#include <deki/Engine.h>
#include <deki/Object.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Deki2D;

namespace
{

std::vector<uint8_t> InterOutlines()
{
    std::vector<uint8_t> bytes;
    EXPECT_TRUE(FontCompiler::CompileOutlineFont(DEKI_TEST_FONT, 32, 126, bytes));
    return bytes;
}

}  // namespace

TEST(OutlineFont, TheEditorMakesASmallFile)
{
    const std::vector<uint8_t> bytes = InterOutlines();
    ASSERT_TRUE(OutlineFont::IsOutlineFile(bytes.data(), bytes.size()));
    std::unique_ptr<OutlineFont> font(OutlineFont::FromFileData(bytes.data(), bytes.size()));
    ASSERT_TRUE(font);
    EXPECT_EQ(font->GlyphCount(), 95u);  // every printable ASCII character
    EXPECT_EQ(font->Codepoints()[0], static_cast<uint32_t>(' '));
    // Every size from one file, and less than two baked sizes take.
    EXPECT_LT(bytes.size(), 40u * 1024u);
    std::printf("[ INFO     ] Inter SemiBold, ASCII: %u bytes of outlines\n", static_cast<unsigned>(bytes.size()));
}

TEST(OutlineFont, BrokenFilesAreRefused)
{
    std::vector<uint8_t> bytes = InterOutlines();
    // Cut short.
    EXPECT_FALSE(OutlineFont::FromFileData(bytes.data(), bytes.size() - 10));
    // A contour that ends past the glyph's points.
    OutlineFontHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    const size_t contoursAt =
        sizeof(header) + header.glyphCount * sizeof(uint32_t) + header.glyphCount * sizeof(OutlineGlyph);
    std::vector<uint8_t> broken = bytes;
    const uint16_t far = 0xFFFF;
    std::memcpy(broken.data() + contoursAt, &far, sizeof(far));
    EXPECT_FALSE(OutlineFont::FromFileData(broken.data(), broken.size()));
    // Not a font at all.
    const uint8_t junk[64] = { 'D', 'V', 'E', 'C' };
    EXPECT_FALSE(OutlineFont::FromFileData(junk, sizeof(junk)));
}

TEST(OutlineFont, GlyphsMatchFreeTypeUnhinted)
{
    const std::vector<uint8_t> bytes = InterOutlines();
    std::unique_ptr<OutlineFont> font(OutlineFont::FromFileData(bytes.data(), bytes.size()));
    ASSERT_TRUE(font);

    FT_Library ft;
    ASSERT_EQ(FT_Init_FreeType(&ft), 0);
    FT_Face face;
    ASSERT_EQ(FT_New_Face(ft, DEKI_TEST_FONT, 0, &face), 0);

    for (const int size : { 10, 16, 33 })
    {
        FT_Set_Pixel_Sizes(face, 0, size);
        const float scale = static_cast<float>(size) / static_cast<float>(font->Header().unitsPerEm);
        double worstMean = 0.0;
        int worstPixel = 0;
        for (const char c : std::string("aegHQ@&%8s"))
        {
            ASSERT_EQ(FT_Load_Char(face, static_cast<FT_ULong>(c), FT_LOAD_RENDER | FT_LOAD_NO_HINTING), 0);
            const FT_GlyphSlot slot = face->glyph;
            const int w = static_cast<int>(slot->bitmap.width);
            const int h = static_cast<int>(slot->bitmap.rows);

            // Ours, drawn two pixels in on every side of FreeType's box.
            uint32_t index = 0;
            while (font->Codepoints()[index] != static_cast<uint32_t>(c))
            {
                ++index;
            }
            const int pad = 2;
            std::vector<uint8_t> ours(static_cast<size_t>((w + 2 * pad) * (h + 2 * pad)), 0);
            ASSERT_TRUE(font->Rasterize(index, scale, scale, static_cast<float>(pad - slot->bitmap_left),
                                        static_cast<float>(pad + slot->bitmap_top), ours.data(), w + 2 * pad,
                                        h + 2 * pad, w + 2 * pad));

            long total = 0;
            int inked = 0;
            for (int y = -pad; y < h + pad; ++y)
            {
                for (int x = -pad; x < w + pad; ++x)
                {
                    const int theirs =
                        (x >= 0 && y >= 0 && x < w && y < h) ? slot->bitmap.buffer[y * slot->bitmap.pitch + x] : 0;
                    const int mine = ours[static_cast<size_t>((y + pad) * (w + 2 * pad) + x + pad)];
                    const int diff = std::abs(theirs - mine);
                    total += diff;
                    inked += (theirs > 0 || mine > 0) ? 1 : 0;
                    worstPixel = std::max(worstPixel, diff);
                }
            }
            const double mean = inked ? static_cast<double>(total) / inked : 0.0;
            worstMean = std::max(worstMean, mean);
            // Both cover each pixel by the share of it inside the outline;
            // only how the curves are cut into lines differs, a little on
            // many edge pixels and never much on any.
            EXPECT_LT(mean, 10.0) << "'" << c << "' at " << size << " px";
        }
        EXPECT_LT(worstPixel, 36) << size << " px";
        std::printf("[ INFO     ] %d px: worst glyph differs by %.2f on average, %d at most (of 255)\n", size,
                    worstMean, worstPixel);
    }
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

TEST(OutlineFont, ASizedFontMeasuresAsABakedOne)
{
    const std::vector<uint8_t> bytes = InterOutlines();
    std::unique_ptr<BitmapFont> font(BitmapFont::LoadFromFileData(bytes.data(), bytes.size()));
    ASSERT_TRUE(font);
    ASSERT_TRUE(font->IsScalable());

    for (const int size : { 10, 16, 24 })
    {
        FontCompiler::CompileOptions options;
        options.fontSize = size;
        options.hinting = FontCompiler::HintingMode::None;
        FontCompiler::CompileResult baked;
        ASSERT_TRUE(FontCompiler::CompileTrueTypeFont(DEKI_TEST_FONT, options, baked));

        const BitmapFont* sized = font->AtSize(size);
        ASSERT_TRUE(sized);
        EXPECT_EQ(sized, font->AtSize(size));  // made once, then kept
        EXPECT_NEAR(sized->GetLineHeight(), baked.lineHeight, 1) << size << " px";
        EXPECT_NEAR(sized->GetBaseline(), baked.baseline, 1) << size << " px";
        EXPECT_NEAR(sized->GetCapHeight(), baked.capHeight, 1) << size << " px";
        for (int c = 'A'; c <= 'z'; ++c)
        {
            const GlyphInfo* glyph = sized->GetGlyphByCodepoint(static_cast<uint32_t>(c));
            ASSERT_TRUE(glyph);
            EXPECT_NEAR(glyph->advance, baked.glyphs[c - options.firstChar].advance, 1)
                << "'" << static_cast<char>(c) << "' at " << size << " px";
        }
    }
    // Any size in between too, and sizes past the range are held to it.
    EXPECT_TRUE(font->AtSize(13));
    EXPECT_EQ(font->AtSize(1000), font->AtSize(BitmapFont::kMaxScalableSize));
}

TEST(OutlineFont, OnlyTheGlyphsATextUsesAreDrawn)
{
    const std::vector<uint8_t> bytes = InterOutlines();
    std::unique_ptr<BitmapFont> font(BitmapFont::LoadFromFileData(bytes.data(), bytes.size()));
    ASSERT_TRUE(font);
    const BitmapFont* sized = font->AtSize(20);
    ASSERT_TRUE(sized);
    EXPECT_EQ(sized->GetAtlas()->data, nullptr);  // nothing yet

    sized->PrepareText("Hi", 2);
    const Deki::Texture2D* atlas = sized->GetAtlas();
    ASSERT_NE(atlas->data, nullptr);
    const int32_t rowsForTwo = atlas->height;
    const GlyphInfo* h = sized->GetGlyphByCodepoint('H');
    const GlyphInfo* i = sized->GetGlyphByCodepoint('i');
    EXPECT_NE(h->x, i->x);  // side by side
    // H's pixels, kept as the atlas grows.
    std::vector<uint8_t> before;
    for (int y = 0; y < h->height; ++y)
    {
        const uint8_t* row = atlas->data + (h->y + y) * atlas->width + h->x;
        before.insert(before.end(), row, row + h->width);
    }
    uint32_t inked = 0;
    for (const uint8_t value : before)
    {
        inked += value > 128 ? 1 : 0;
    }
    EXPECT_GT(inked, 10u);  // H is drawn

    std::string all;
    for (int c = 32; c <= 126; ++c)
    {
        all += static_cast<char>(c);
    }
    const auto started = std::chrono::steady_clock::now();
    sized->PrepareText(all.data(), all.size());
    const double micros =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    std::printf("[ INFO     ] the other 93 glyphs at 20 px drawn in %.0f us on this computer\n", micros);
    EXPECT_GT(atlas->height, rowsForTwo);
    std::vector<uint8_t> after;
    for (int y = 0; y < h->height; ++y)
    {
        const uint8_t* row = atlas->data + (h->y + y) * atlas->width + h->x;
        after.insert(after.end(), row, row + h->width);
    }
    EXPECT_EQ(before, after);
    std::printf("[ INFO     ] 95 glyphs at 20 px: a %dx%d atlas (%d bytes)\n", static_cast<int>(atlas->width),
                static_cast<int>(atlas->height), static_cast<int>(atlas->width * atlas->height));
}

TEST(OutlineFont, ATextDrawsAtAnySize)
{
    const std::vector<uint8_t> bytes = InterOutlines();
    std::unique_ptr<BitmapFont> font(BitmapFont::LoadFromFileData(bytes.data(), bytes.size()));
    ASSERT_TRUE(font);

    Deki::Object obj("Label");
    auto* text = obj.AddComponent<TextComponent>();
    text->SetFont(font.get());
    text->SetText("Hello there");
    text->SetWidth(20.0f);
    text->SetHeight(6.0f);
    text->SetFontSize(14);

    QuadBlit::Source source;
    float pivotX = 0.0f;
    float pivotY = 0.0f;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    ASSERT_TRUE(text->RenderContent(&obj, source, pivotX, pivotY, r, g, b, a));
    const int32_t smallWidth = text->GetTextWidth();
    const int32_t smallInk = source.width;
    EXPECT_EQ(smallWidth, font->AtSize(14)->MeasureWidth("Hello there"));
    EXPECT_GT(smallInk, 0);

    text->SetFontSize(28);
    ASSERT_TRUE(text->RenderContent(&obj, source, pivotX, pivotY, r, g, b, a));
    const double ratio = static_cast<double>(text->GetTextWidth()) / smallWidth;
    EXPECT_NEAR(ratio, 2.0, 0.15);
    EXPECT_NEAR(static_cast<double>(source.width) / smallInk, 2.0, 0.2);
}

TEST(OutlineFont, ANewColourKeepsTheBake)
{
    // A one-colour text is coverage tinted as it is drawn: changing its
    // colour must not draw the glyphs again.
    const std::vector<uint8_t> bytes = InterOutlines();
    std::unique_ptr<BitmapFont> font(BitmapFont::LoadFromFileData(bytes.data(), bytes.size()));
    ASSERT_TRUE(font);
    Deki::Object obj("Label");
    auto* text = obj.AddComponent<TextComponent>();
    text->SetFont(font.get());
    text->SetText("Tint");
    text->SetWidth(10.0f);
    text->SetHeight(3.0f);

    QuadBlit::Source source;
    float pivotX = 0.0f;
    float pivotY = 0.0f;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    ASSERT_TRUE(text->RenderContent(&obj, source, pivotX, pivotY, r, g, b, a));
    const auto revision = source.revision;
    text->SetColor(Deki::Color(255, 0, 0));
    ASSERT_TRUE(text->RenderContent(&obj, source, pivotX, pivotY, r, g, b, a));
    EXPECT_EQ(source.revision, revision);
    EXPECT_EQ(r, 0xF8);  // the new colour, as the tint
    EXPECT_EQ(g, 0);
}
