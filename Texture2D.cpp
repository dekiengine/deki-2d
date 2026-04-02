#include "Texture2D.h"
#include "providers/DekiMemoryProvider.h"
#ifndef DEKI_EDITOR
#include "providers/DekiFileSystemProvider.h"
#else
#include <cstdlib>
#include <fstream>
#endif
#include "DekiLogSystem.h"
#include <cstring>

Texture2D::Texture2D()
: data(nullptr), width(0), height(0), format(Texture2D::TextureFormat::RGB565), has_transparency(false), has_alpha(false)
#ifdef DEKI_EDITOR
, allocated_with_backend(false)
#endif
{
}

Texture2D::~Texture2D()
{
    if (data)
    {
#ifdef DEKI_EDITOR
        // Editor: Free using the same allocator that was used for allocation
        // Play mode uses DekiMemoryProvider, edit mode uses std::free
        if (allocated_with_backend)
        {
            DekiMemoryProvider::Free(data);
        }
        else
        {
            std::free(data);
        }
#else
        // Runtime: Always use DekiMemoryProvider
        DekiMemoryProvider::Free(data);
#endif
        data = nullptr;
    }
}

#ifndef DEKI_EDITOR
Texture2D* Texture2D::Load(const char* file_path)
{
    if (!file_path)
    {
        DEKI_LOG_ERROR("NULL file path");
        return nullptr;
    }

    IDekiFileSystem* fs = DekiFileSystemProvider::GetFileSystemForPath(file_path);
    if (!fs) {
        DEKI_LOG_DEBUG("FileSystem not initialized for path: %s", file_path);
        return nullptr;
    }

    // Check if file exists
    if (!fs->FileExists(file_path))
    {
        DEKI_LOG_ERROR("Texture file does not exist: %s", file_path);
        return nullptr;
    }

    // Open file
    IDekiFileSystem::FileHandle file = fs->OpenFile(file_path, IDekiFileSystem::OpenMode::READ_BINARY);
    if (!file)
    {
        DEKI_LOG_ERROR("Failed to open texture file: %s", file_path);
        return nullptr;
    }

    // Get file size
    long file_size = fs->GetFileSize(file);
    if (file_size < sizeof(Texture2D::Header))
    {
        DEKI_LOG_ERROR("File too small to contain texture header: %s", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    // Read header
    Texture2D::Header header;
    size_t bytes_read = fs->ReadFile(file, &header, sizeof(Texture2D::Header));
    if (bytes_read != sizeof(Texture2D::Header))
    {
        DEKI_LOG_ERROR("Failed to read texture header: %s", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    // Validate header
    if (!ValidateHeader(header))
    {
        DEKI_LOG_ERROR("Invalid texture header: %s", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    // Validate file size
    size_t expected_size = sizeof(Texture2D::Header) + header.data_size + header.metadata_size;
    if (file_size < expected_size)
    {
        DEKI_LOG_ERROR("File size mismatch. Expected: %zu, Got: %ld", expected_size, file_size);
        fs->CloseFile(file);
        return nullptr;
    }

    // Read pixel data
    uint8_t* pixel_data = (uint8_t*)DekiMemoryProvider::Allocate(
        header.data_size, true, "Texture2D::Load");

    if (!pixel_data)
    {
        DEKI_LOG_ERROR("Failed to allocate memory for texture data");
        fs->CloseFile(file);
        return nullptr;
    }

    bytes_read = fs->ReadFile(file, pixel_data, header.data_size);
    if (bytes_read != header.data_size)
    {
        DEKI_LOG_ERROR("Failed to read texture pixel data");
        DekiMemoryProvider::Free(pixel_data);
        fs->CloseFile(file);
        return nullptr;
    }

    fs->CloseFile(file);

    // Create texture instance
    Texture2D* texture = new Texture2D();
    if (!texture->LoadFromMemory(header, pixel_data))
    {
        DEKI_LOG_ERROR("Failed to load texture from memory");
        DekiMemoryProvider::Free(pixel_data);
        delete texture;
        return nullptr;
    }

    // Pixel data is now owned by texture
    texture->data = pixel_data;

    DEKI_LOG_DEBUG("Loaded texture: %s (%dx%d, %s)",
              file_path,
              texture->width,
              texture->height,
              GetFormatName(texture->format));

    return texture;
}
#endif  // !DEKI_EDITOR

bool Texture2D::LoadFromMemory(const Texture2D::Header& header, const uint8_t* pixel_data)
{
    if (!pixel_data)
    {
        return false;
    }

    // Set basic properties
    width = header.width;
    height = header.height;
    format = header.format;
    has_transparency = (header.flags & DTEX_FLAG_HAS_TRANSPARENCY) != 0;
    has_alpha = (header.flags & DTEX_FLAG_HAS_ALPHA) != 0;

    return true;
}

uint32_t Texture2D::GetBytesPerPixel(Texture2D::TextureFormat format)
{
    switch (format)
    {
        case Texture2D::TextureFormat::RGB888:
            return 3;
        case Texture2D::TextureFormat::RGBA8888:
            return 4;
        case Texture2D::TextureFormat::RGB565:
            return 2;
        case Texture2D::TextureFormat::RGB565A8:
            return 3;
        case Texture2D::TextureFormat::ALPHA8:
            return 1;
        default:
            return 0;
    }
}

const char* Texture2D::GetFormatName(Texture2D::TextureFormat format)
{
    switch (format)
    {
        case Texture2D::TextureFormat::RGB888:
            return "RGB888";
        case Texture2D::TextureFormat::RGBA8888:
            return "RGBA8888";
        case Texture2D::TextureFormat::RGB565:
            return "RGB565";
        case Texture2D::TextureFormat::RGB565A8:
            return "RGB565A8";
        case Texture2D::TextureFormat::ALPHA8:
            return "ALPHA8";
        default:
            return "Unknown";
    }
}

bool Texture2D::ValidateHeader(const Texture2D::Header& header)
{
    // Check magic number
    if (strncmp(header.magic, "2DTX", 4) != 0)
    {
        DEKI_LOG_ERROR("Invalid magic number in texture header");
        return false;
    }

    // Check version
    if (header.version != 1)
    {
        DEKI_LOG_ERROR("Unsupported texture version: %u", header.version);
        return false;
    }

    // Check dimensions
    if (header.width == 0 || header.height == 0)
    {
        DEKI_LOG_ERROR("Invalid texture dimensions: %ux%u", header.width, header.height);
        return false;
    }

    // Check format
    uint32_t bytes_per_pixel = GetBytesPerPixel(header.format);
    if (bytes_per_pixel == 0)
    {
        DEKI_LOG_ERROR("Unknown texture format: %u", static_cast<uint32_t>(header.format));
        return false;
    }

    // Validate data size
    uint32_t expected_data_size = header.width * header.height * bytes_per_pixel;
    if (header.data_size != expected_data_size)
    {
        DEKI_LOG_ERROR("Data size mismatch. Expected: %u, Got: %u", expected_data_size, header.data_size);
        return false;
    }

    return true;
}

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
    file.read(reinterpret_cast<char*>(&header.data_size), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&header.metadata_size), sizeof(uint32_t));
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
    uint8_t* pixel_data = new uint8_t[header.data_size];
    file.read(reinterpret_cast<char*>(pixel_data), header.data_size);

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

    DEKI_LOG_DEBUG("Loaded texture as RGBA: %s (%dx%d, %s)",
              file_path,
              out_width,
              out_height,
              GetFormatName(header.format));

    return rgba_data;
}
#endif  // DEKI_EDITOR