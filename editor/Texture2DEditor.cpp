// Editor-only Texture2D helpers: RGBA conversion and direct file loads for
// previews. Kept out of Texture2D.cpp so the runtime file has no editor paths.

#include <deki/assets/Texture2D.h>
#include <deki/providers/Memory.h>
#include <deki/LogSystem.h>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef DEKI_EDITOR
uint8_t* Deki::Texture2D::ConvertToRGBA(const uint8_t* srcData, int32_t width, int32_t height, TextureFormat format)
{
    if (!srcData || width <= 0 || height <= 0)
    {
        return nullptr;
    }

    size_t pixelCount = static_cast<size_t>(width) * height;
    uint8_t* rgba = new uint8_t[pixelCount * 4];

    size_t srcIdx = 0;
    size_t dstIdx = 0;

    for (size_t i = 0; i < pixelCount; ++i)
    {
        uint8_t r, g, b, a = 255;

        switch (format)
        {
            case TextureFormat::RGB888:
                r = srcData[srcIdx++];
                g = srcData[srcIdx++];
                b = srcData[srcIdx++];
                break;

            case TextureFormat::RGBA8888:
                r = srcData[srcIdx++];
                g = srcData[srcIdx++];
                b = srcData[srcIdx++];
                a = srcData[srcIdx++];
                break;

            case TextureFormat::RGB565:
            {
                uint16_t rgb565 = srcData[srcIdx] | (srcData[srcIdx + 1] << 8);
                srcIdx += 2;

                r = static_cast<uint8_t>(((rgb565 >> 11) & 0x1F) << 3);
                g = static_cast<uint8_t>(((rgb565 >> 5) & 0x3F) << 2);
                b = static_cast<uint8_t>((rgb565 & 0x1F) << 3);

                // Fill the low bits from the high ones, for accurate color.
                r |= (r >> 5);
                g |= (g >> 6);
                b |= (b >> 5);
                break;
            }

            case TextureFormat::RGB565A8:
            {
                uint16_t rgb565 = srcData[srcIdx] | (srcData[srcIdx + 1] << 8);
                srcIdx += 2;
                a = srcData[srcIdx++];

                r = static_cast<uint8_t>(((rgb565 >> 11) & 0x1F) << 3);
                g = static_cast<uint8_t>(((rgb565 >> 5) & 0x3F) << 2);
                b = static_cast<uint8_t>((rgb565 & 0x1F) << 3);

                r |= (r >> 5);
                g |= (g >> 6);
                b |= (b >> 5);
                break;
            }

            case TextureFormat::ALPHA8:
                r = g = b = 255;
                a = srcData[srcIdx++];
                break;

            default: r = g = b = a = 255; break;
        }

        rgba[dstIdx++] = r;
        rgba[dstIdx++] = g;
        rgba[dstIdx++] = b;
        rgba[dstIdx++] = a;
    }

    return rgba;
}

uint8_t* Deki::Texture2D::LoadAsRGBA(const char* filePath, int32_t& outWidth, int32_t& outHeight, bool& outHasAlpha)
{
    outWidth = 0;
    outHeight = 0;
    outHasAlpha = false;

    if (!filePath)
    {
        DEKI_LOG_ERROR("NULL file path");
        return nullptr;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        DEKI_LOG_ERROR("Failed to open texture file: %s", filePath);
        return nullptr;
    }

    Header header;
    file.read(header.magic, 4);
    file.read(reinterpret_cast<char*>(&header.version), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&header.width), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&header.height), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&header.format), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&header.dataSize), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&header.metadataSize), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&header.flags), sizeof(uint32_t));

    if (!file.good())
    {
        DEKI_LOG_ERROR("Failed to read texture header: %s", filePath);
        return nullptr;
    }

    if (!ValidateHeader(header))
    {
        DEKI_LOG_ERROR("Invalid texture header: %s", filePath);
        return nullptr;
    }

    uint8_t* pixelData = new uint8_t[header.dataSize];
    file.read(reinterpret_cast<char*>(pixelData), header.dataSize);

    if (!file.good() && !file.eof())
    {
        DEKI_LOG_ERROR("Failed to read texture pixel data: %s", filePath);
        delete[] pixelData;
        return nullptr;
    }

    uint8_t* rgbaData = ConvertToRGBA(pixelData, header.width, header.height, header.format);
    delete[] pixelData;

    if (!rgbaData)
    {
        DEKI_LOG_ERROR("Failed to convert texture to RGBA: %s", filePath);
        return nullptr;
    }

    outWidth = header.width;
    outHeight = header.height;
    outHasAlpha = (header.flags & DTEX_FLAG_HAS_ALPHA) != 0;

    DEKI_LOG_INTERNAL("Loaded texture as RGBA: %s (%dx%d, %s)", filePath, outWidth, outHeight,
                      GetFormatName(header.format));

    return rgbaData;
}
#endif  // DEKI_EDITOR
