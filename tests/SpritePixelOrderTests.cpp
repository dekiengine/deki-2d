// A .dtex keeps RGB565 low byte first. Once loaded, a sprite's pixels are in
// memory's byte order (Deki::kRGB565BigEndian), the order the blitter reads,
// and so are the sprites made in code. These run in both byte orders.

#include <gtest/gtest.h>

#include <Sprite.h>
#include <PixelFormat.h>
#include <deki/assets/Texture2D.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Deki2D;

namespace
{
using Format = Deki::Texture2D::TextureFormat;

std::vector<uint8_t> Dtex(Format format, uint32_t w, uint32_t h, const std::vector<uint8_t>& pixels)
{
    Deki::Texture2D::Header header{};
    std::memcpy(header.magic, "2DTX", 4);
    header.version = 1;
    header.width = w;
    header.height = h;
    header.format = format;
    header.dataSize = static_cast<uint32_t>(pixels.size());
    header.metadataSize = 0;
    header.flags = format == Format::RGB565A8 ? DTEX_FLAG_HAS_ALPHA : 0;
    std::vector<uint8_t> file(sizeof(header) + pixels.size());
    std::memcpy(file.data(), &header, sizeof(header));
    std::memcpy(file.data() + sizeof(header), pixels.data(), pixels.size());
    return file;
}
}  // namespace

TEST(SpritePixelOrder, LoadedRgb565IsInMemoryOrder)
{
    // 0xF800 and 0x1234, low byte first as the file keeps them.
    const std::vector<uint8_t> file = Dtex(Format::RGB565, 2, 1, { 0x00, 0xF8, 0x34, 0x12 });
    std::unique_ptr<Sprite> sprite(Sprite::LoadFromFileData(file.data(), file.size()));
    ASSERT_TRUE(sprite);
    EXPECT_EQ(DekiPixel::Load565(sprite->data), 0xF800);
    EXPECT_EQ(DekiPixel::Load565(sprite->data + 2), 0x1234);
}

TEST(SpritePixelOrder, LoadedRgb565A8KeepsAlphaAfterTheColour)
{
    const std::vector<uint8_t> file = Dtex(Format::RGB565A8, 2, 1, { 0x34, 0x12, 0x80, 0x00, 0xF8, 0xFF });
    std::unique_ptr<Sprite> sprite(Sprite::LoadFromFileData(file.data(), file.size()));
    ASSERT_TRUE(sprite);
    EXPECT_EQ(DekiPixel::Load565(sprite->data), 0x1234);
    EXPECT_EQ(sprite->data[2], 0x80);
    EXPECT_EQ(DekiPixel::Load565(sprite->data + 3), 0xF800);
    EXPECT_EQ(sprite->data[5], 0xFF);
}

TEST(SpritePixelOrder, OtherFormatsAreLeftAsTheyAre)
{
    const std::vector<uint8_t> file = Dtex(Format::RGB888, 1, 1, { 0x01, 0x02, 0x03 });
    std::unique_ptr<Sprite> sprite(Sprite::LoadFromFileData(file.data(), file.size()));
    ASSERT_TRUE(sprite);
    EXPECT_EQ(sprite->data[0], 0x01);
    EXPECT_EQ(sprite->data[1], 0x02);
    EXPECT_EQ(sprite->data[2], 0x03);
}

TEST(SpritePixelOrder, SolidSpritesAreInMemoryOrderAndPackedAsEverywhereElse)
{
    std::unique_ptr<Sprite> solid(Sprite::CreateSolid(2, 1, 200, 100, 50));
    ASSERT_TRUE(solid);
    EXPECT_EQ(DekiPixel::Load565(solid->data + 2), DekiPixel::PackRGB565(200, 100, 50));

    std::unique_ptr<Sprite> rgba(Sprite::CreateSolidRGBA(2, 1, 200, 100, 50, 77));
    ASSERT_TRUE(rgba);
    EXPECT_EQ(DekiPixel::Load565(rgba->data + 3), DekiPixel::PackRGB565(200, 100, 50));
    EXPECT_EQ(rgba->data[5], 77);
}
