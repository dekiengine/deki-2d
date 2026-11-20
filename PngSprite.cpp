#include "PngSprite.h"

#include "PixelFormat.h"

#include <deki/providers/Memory.h>

#include <memory>

namespace Deki2D
{

Sprite* CreateSpriteFromPng(const uint8_t* data, size_t size, int32_t maxWidth, int32_t maxHeight, Png::Result* result)
{
    Png::Result ignored;
    Png::Result& why = result ? *result : ignored;
    Png::Info info;
    why = Png::ReadInfo(data, size, info);
    if (why != Png::Result::Ok)
    {
        return nullptr;
    }
    uint32_t width = 0;
    uint32_t height = 0;
    Png::FitSize(info.width, info.height, maxWidth > 0 ? static_cast<uint32_t>(maxWidth) : 0,
                 maxHeight > 0 ? static_cast<uint32_t>(maxHeight) : 0, width, height);

    std::unique_ptr<Sprite> sprite(new Sprite());
    sprite->width = static_cast<int32_t>(width);
    sprite->height = static_cast<int32_t>(height);
    sprite->format = info.hasAlpha ? Deki::Texture2D::TextureFormat::RGB565A8 : Deki::Texture2D::TextureFormat::RGB565;
    sprite->hasTransparency = false;
    sprite->hasAlpha = info.hasAlpha;
    const size_t bytesPerPixel = info.hasAlpha ? 3 : 2;
    sprite->data = static_cast<uint8_t*>(
        Deki::Memory::Allocate(static_cast<size_t>(width) * height * bytesPerPixel, Deki::Memory::External));
#ifdef DEKI_EDITOR
    sprite->allocatedWithBackend = true;  // freed with Deki::Memory, as the loaders' are
#endif
    if (!sprite->data)
    {
        why = Png::Result::NoMemory;
        return nullptr;
    }

    uint8_t* pixels = sprite->data;
    uint32_t outWidth = 0;
    uint32_t outHeight = 0;
    why = Png::Decode(
        data, size, width, height,
        [&](uint32_t y, const uint8_t* rgba, uint32_t rowWidth)
        {
            uint8_t* out = pixels + static_cast<size_t>(y) * rowWidth * bytesPerPixel;
            for (uint32_t x = 0; x < rowWidth; ++x)
            {
                const uint8_t* p = rgba + x * 4;
                DekiPixel::Store565(out + x * bytesPerPixel, DekiPixel::PackRGB565(p[0], p[1], p[2]));
                if (bytesPerPixel == 3)
                {
                    out[x * 3 + 2] = p[3];
                }
            }
        },
        outWidth, outHeight);
    if (why != Png::Result::Ok || outWidth != width || outHeight != height)
    {
        why = why == Png::Result::Ok ? Png::Result::Corrupt : why;
        return nullptr;
    }
    return sprite.release();
}

}  // namespace Deki2D
