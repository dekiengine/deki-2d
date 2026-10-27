// The runs a loaded sprite with soft edges carries for QuadBlit: each row's
// transparent run (skipped unread) and its run of one opaque colour (filled
// without reading). They only save work: a sprite drawn with them must come
// out exactly as it does without them, tinted, faded or plain.

#include <gtest/gtest.h>

#include <Sprite.h>
#include <SpriteRowSpans.h>
#include <PixelFormat.h>
#include <QuadBlit.h>
#include <deki/assets/Texture2D.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <vector>

using namespace Deki2D;

namespace
{
using Format = Deki::Texture2D::TextureFormat;

std::vector<uint8_t> Dtex(uint32_t w, uint32_t h, const std::vector<uint8_t>& pixels)
{
    Deki::Texture2D::Header header{};
    std::memcpy(header.magic, "2DTX", 4);
    header.version = 1;
    header.width = w;
    header.height = h;
    header.format = Format::RGB565A8;
    header.dataSize = static_cast<uint32_t>(pixels.size());
    header.metadataSize = 0;
    header.flags = DTEX_FLAG_HAS_ALPHA;
    std::vector<uint8_t> file(sizeof(header) + pixels.size());
    std::memcpy(file.data(), &header, sizeof(header));
    std::memcpy(file.data() + sizeof(header), pixels.data(), pixels.size());
    return file;
}

// A 34-pixel disc, white inside, with a soft rim and clear corners: the
// menu's bubble. Low byte first, as a .dtex keeps it.
std::vector<uint8_t> Disc(int32_t size)
{
    std::vector<uint8_t> px;
    const float c = (size - 1) * 0.5f;
    const float r = size * 0.5f;
    for (int32_t y = 0; y < size; y++)
    {
        for (int32_t x = 0; x < size; x++)
        {
            const float dx = x - c, dy = y - c;
            const float d = r - __builtin_sqrtf(dx * dx + dy * dy);
            const int32_t a = d >= 2.0f ? 255 : (d <= 0.0f ? 0 : static_cast<int32_t>(d * 127.5f));
            px.push_back(0xFF);
            px.push_back(0xFF);
            px.push_back(static_cast<uint8_t>(a));
        }
    }
    return px;
}

// A background no blend can get right by luck: every pixel different.
std::vector<uint16_t> Background(int32_t w, int32_t h)
{
    std::vector<uint16_t> px(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < px.size(); i++)
    {
        px[i] = DekiPixel::ToMemory565(static_cast<uint16_t>(i * 2654435761u >> 7));
    }
    return px;
}

std::vector<uint16_t> Draw(const QuadBlit::Source& src, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    constexpr int32_t kW = 48, kH = 44;
    std::vector<uint16_t> target = Background(kW, kH);
    QuadBlit::Blit(src, reinterpret_cast<uint8_t*>(target.data()), kW, kH, Deki::ColorFormat::RGB565, 24, 22, 1.0f,
                   1.0f, 0.0f, 0.5f, 0.5f, r, g, b, a, false);
    return target;
}

std::vector<int16_t> ClearSpansOf(std::initializer_list<uint8_t> alphas)
{
    std::vector<uint8_t> row;
    for (uint8_t a : alphas)
    {
        row.insert(row.end(), { 0xAB, 0xCD, a });
    }
    std::vector<int16_t> spans(2, -1);
    BuildClearRowSpans(row.data(), static_cast<int32_t>(alphas.size()), 1, static_cast<int32_t>(row.size()),
                       spans.data());
    return spans;
}
}  // namespace

TEST(SpriteRuns, TheLongestTransparentRunIsTheClearSpan)
{
    EXPECT_EQ(ClearSpansOf({ 0, 0, 255, 0, 0, 0, 128 }), (std::vector<int16_t>{ 3, 6 }));
    EXPECT_EQ(ClearSpansOf({ 1, 0, 0, 1 }), (std::vector<int16_t>{ 1, 3 }));
}

TEST(SpriteRuns, ARowWithNoTransparentPixelHasAnEmptyClearSpan)
{
    EXPECT_EQ(ClearSpansOf({ 255, 1, 128 }), (std::vector<int16_t>{ 0, 0 }));
}

TEST(SpriteRuns, ASoftSpriteGetsItsRunsAtLoad)
{
    const std::vector<uint8_t> file = Dtex(34, 34, Disc(34));
    std::unique_ptr<Sprite> sprite(Sprite::LoadFromFileData(file.data(), file.size()));
    ASSERT_TRUE(sprite);
    ASSERT_TRUE(sprite->hasAlpha);
    ASSERT_TRUE(sprite->clearRowSpans);
    ASSERT_TRUE(sprite->uniformRuns);
    // The top row is mostly corner; the middle row is mostly white.
    EXPECT_GT(sprite->clearRowSpans.Data()[1] - sprite->clearRowSpans.Data()[0], 0);
    EXPECT_GT(sprite->uniformRuns.Data()[17].end - sprite->uniformRuns.Data()[17].start, 20);
}

TEST(SpriteRuns, AnOpaqueSpriteHasNone)
{
    std::vector<uint8_t> px;
    for (int32_t i = 0; i < 16; i++)
    {
        px.insert(px.end(), { 0x34, 0x12, 255 });
    }
    const std::vector<uint8_t> file = Dtex(4, 4, px);
    std::unique_ptr<Sprite> sprite(Sprite::LoadFromFileData(file.data(), file.size()));
    ASSERT_TRUE(sprite);
    EXPECT_FALSE(sprite->hasAlpha);
    EXPECT_FALSE(sprite->clearRowSpans);
    EXPECT_FALSE(sprite->uniformRuns);
}

TEST(SpriteRuns, DrawingWithTheRunsGivesTheSamePixels)
{
    const std::vector<uint8_t> file = Dtex(34, 34, Disc(34));
    std::unique_ptr<Sprite> sprite(Sprite::LoadFromFileData(file.data(), file.size()));
    ASSERT_TRUE(sprite);

    QuadBlit::Source with = QuadBlit::MakeSource(sprite->data, sprite->width, sprite->height,
                                                 QuadBlit::PixelLayout::FromTexture(sprite->format, sprite->hasAlpha),
                                                 false, sprite->alphaRowSpans.Data());
    with.clearRowSpans = sprite->clearRowSpans.Data();
    with.uniformRuns = sprite->uniformRuns.Data();
    QuadBlit::Source without = with;
    without.clearRowSpans = nullptr;
    without.uniformRuns = nullptr;

    // Plain, tinted, faded, and both: each takes its own path through QuadBlit.
    struct Tint
    {
        uint8_t r, g, b, a;
    };
    for (const Tint t :
         { Tint{ 255, 255, 255, 255 }, Tint{ 52, 211, 153, 255 }, Tint{ 255, 255, 255, 42 }, Tint{ 52, 211, 153, 42 } })
    {
        EXPECT_EQ(Draw(with, t.r, t.g, t.b, t.a), Draw(without, t.r, t.g, t.b, t.a))
            << "tint " << int(t.r) << "," << int(t.g) << "," << int(t.b) << " alpha " << int(t.a);
    }
}
