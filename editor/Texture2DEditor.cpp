/**
 * @file Texture2DEditor.cpp
 * @brief Editor-only Texture2D helpers: RGBA conversion and direct file loads
 *        for previews. Moved out of Texture2D.cpp so the runtime file stays
 *        free of editor paths.
 */

#include "Texture2D.h"
#include <deki/providers/Memory.h>
#include <deki/LogSystem.h>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef DEKI_EDITOR
uint8_t* Texture2D::ConvertToRGBA(const uint8_t* src_data, int32_t width, int32_t height, TextureFormat format)
{
    if (!src_data || width <= 0 || height <= 0)
    {
        return nullptr;
    }

    size_t pixel_count = static_cast<size_t>(width) * height;
    uint8_t* rgba = new uint8_t[pixel_count * 4];

    size_t src_idx = 0;
    size_t dst_idx = 0;

    for (size_t i = 0; i < pixel_count; ++i)
    {
        uint8_t r, g, b, a = 255;

        switch (format)
        {
            case TextureFormat::RGB888:
                r = src_data[src_idx++];
                g = src_data[src_idx++];
                b = src_data[src_idx++];
                break;

            case TextureFormat::RGBA8888:
                r = src_data[src_idx++];
                g = src_data[src_idx++];
                b = src_data[src_idx++];
                a = src_data[src_idx++];
                break;

            case TextureFormat::RGB565:
            {
                uint16_t rgb565 = src_data[src_idx] | (src_data[src_idx + 1] << 8);
                src_idx += 2;

                r = static_cast<uint8_t>(((rgb565 >> 11) & 0x1F) << 3);
                g = static_cast<uint8_t>(((rgb565 >> 5) & 0x3F) << 2);
                b = static_cast<uint8_t>((rgb565 & 0x1F) << 3);

                // Fill low bits for better color accuracy
                r |= (r >> 5);
                g |= (g >> 6);
                b |= (b >> 5);
                break;
            }

            case TextureFormat::RGB565A8:
            {
                uint16_t rgb565 = src_data[src_idx] | (src_data[src_idx + 1] << 8);
                src_idx += 2;
                a = src_data[src_idx++];

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
                a = src_data[src_idx++];
                break;

            default:
                r = g = b = a = 255;
                break;
        }

        rgba[dst_idx++] = r;
        rgba[dst_idx++] = g;
        rgba[dst_idx++] = b;
        rgba[dst_idx++] = a;
    }

    return rgba;
}

uint8_t* Texture2D::LoadAsRGBA(const char* file_path, int32_t& out_width, int32_t& out_height, bool& out_has_alpha)
{
    out_width = 0;
    out_height = 0;
    out_has_alpha = false;

    if (!file_path)
    {
        DEKI_LOG_ERROR("NULL file path");
        return nullptr;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open())
    {
        DEKI_LOG_ERROR("Failed to open texture file: %s", file_path);
        return nullptr;
    }

    // Read header
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
        DEKI_LOG_ERROR("Failed to read texture header: %s", file_path);
        return nullptr;
    }

    // Validate header
    if (!ValidateHeader(header))
    {
        DEKI_LOG_ERROR("Invalid texture header: %s", file_path);
        return nullptr;
    }

    // Read pixel data
    uint8_t* pixel_data = new uint8_t[header.dataSize];
    file.read(reinterpret_cast<char*>(pixel_data), header.dataSize);

    if (!file.good() && !file.eof())
    {
        DEKI_LOG_ERROR("Failed to read texture pixel data: %s", file_path);
        delete[] pixel_data;
        return nullptr;
    }

    // Convert to RGBA
    uint8_t* rgba_data = ConvertToRGBA(pixel_data, header.width, header.height, header.format);
    delete[] pixel_data;

    if (!rgba_data)
    {
        DEKI_LOG_ERROR("Failed to convert texture to RGBA: %s", file_path);
        return nullptr;
    }

    out_width = header.width;
    out_height = header.height;
    out_has_alpha = (header.flags & DTEX_FLAG_HAS_ALPHA) != 0;

    DEKI_LOG_INTERNAL("Loaded texture as RGBA: %s (%dx%d, %s)",
              file_path,
              out_width,
              out_height,
              GetFormatName(header.format));

    return rgba_data;
}
#endif  // DEKI_EDITOR