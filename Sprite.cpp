#include "Sprite.h"
#include "Texture2D.h"

#include <cstdlib>
#include <cstring>

#include "providers/DekiFileSystemProvider.h"
#include "providers/DekiMemoryProvider.h"
#include "DekiLogSystem.h"
#include "DekiTime.h"
#include "assets/AssetManager.h"

Sprite::Sprite() : Texture2D()
{
    SetDefaultSpriteProperties();
}

Sprite::~Sprite()
{
    // Base class destructor handles data cleanup
}

const SpriteFrame* Sprite::FindFrame(const std::string& guid) const
{
    for (size_t i = 0; i < frames.size(); i++)
    {
        if (guid == frames[i].guid)
            return &frames[i];
    }
    return nullptr;
}

void Sprite::SetDefaultSpriteProperties()
{
    pivot_x = 0.5f;  // Center pivot
    pivot_y = 0.5f;  // Center pivot
    pixels_per_unit = 100.0f;  // Unity-style default
    transparent_r = 255;  // Magenta as default transparent color
    transparent_g = 0;
    transparent_b = 255;

    // 9-slice defaults
    has_nine_slice = false;
    nine_slice_left = 0;
    nine_slice_right = 0;
    nine_slice_top = 0;
    nine_slice_bottom = 0;

    // Spritesheet defaults
    default_frame_width = 0;
    default_frame_height = 0;
}

// Loading functions - same for simulator and editor
Sprite* Sprite::Load(const char* file_path)
{
    if (!file_path)
    {
        DEKI_LOG_ERROR("NULL file path");
        return nullptr;
    }

    uint32_t tStart = DekiTime::GetTime();

    IDekiFileSystem* fs = DekiFileSystemProvider::GetFileSystemForPath(file_path);
    if (!fs) {
        DEKI_LOG_INTERNAL("FileSystem not initialized for path: %s", file_path);
        return nullptr;
    }

    // Open file
    IDekiFileSystem::FileHandle file = fs->OpenFile(file_path, IDekiFileSystem::OpenMode::READ_BINARY);
    if (!file)
    {
        DEKI_LOG_ERROR("Failed to open sprite file: %s", file_path);
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
        DEKI_LOG_ERROR("Failed to read sprite header: %s", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    // Validate header
    if (!Texture2D::ValidateHeader(header))
    {
        DEKI_LOG_ERROR("Invalid sprite header: %s", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    // Non-sprite textures (e.g. font atlases) are loaded through Sprite::Load() — this is normal
    if (!(header.flags & DTEX_FLAG_IS_SPRITE))
    {
        DEKI_LOG_INTERNAL("Loading non-sprite texture as sprite: %s", file_path);
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
        header.data_size, true, "Sprite::Load");

    if (!pixel_data)
    {
        DEKI_LOG_ERROR("Failed to allocate memory for sprite data");
        fs->CloseFile(file);
        return nullptr;
    }

    bytes_read = fs->ReadFile(file, pixel_data, header.data_size);
    if (bytes_read != header.data_size)
    {
        DEKI_LOG_ERROR("Failed to read sprite pixel data");
        DekiMemoryProvider::Free(pixel_data);
        fs->CloseFile(file);
        return nullptr;
    }

    // Read metadata if present
    uint8_t* metadata = nullptr;
    if (header.metadata_size > 0)
    {
        metadata = (uint8_t*)DekiMemoryProvider::Allocate(
            header.metadata_size, false, "Sprite::Load-metadata");

        if (metadata)
        {
            bytes_read = fs->ReadFile(file, metadata, header.metadata_size);
            if (bytes_read != header.metadata_size)
            {
                DEKI_LOG_WARNING("Failed to read sprite metadata, using defaults");
                DekiMemoryProvider::Free(metadata);
                metadata = nullptr;
            }
        }
    }

    fs->CloseFile(file);

    // Create sprite instance
    Sprite* sprite = new Sprite();
    if (!sprite->LoadFromMemory(header, pixel_data))
    {
        DEKI_LOG_ERROR("Failed to load sprite from memory");
        DekiMemoryProvider::Free(pixel_data);
        if (metadata) DekiMemoryProvider::Free(metadata);
        delete sprite;
        return nullptr;
    }

    // Process metadata (chunked format for 2DTX)
    if (metadata && header.metadata_size >= sizeof(uint32_t))
    {
        // Parse chunked metadata
        uint32_t offset = 0;
        uint32_t num_chunks = *(uint32_t*)(metadata + offset);
        offset += sizeof(uint32_t);

        for (uint32_t i = 0; i < num_chunks && offset + 8 <= header.metadata_size; ++i)
        {
            uint32_t chunk_type = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);
            uint32_t chunk_size = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);

            if (offset + chunk_size > header.metadata_size)
                break;  // Corrupted metadata

            if (chunk_type == 1 && chunk_size >= 8)  // Sprite chunk
            {
                int32_t frameWidth = *(int32_t*)(metadata + offset);
                int32_t frameHeight = *(int32_t*)(metadata + offset + sizeof(int32_t));
                sprite->default_frame_width = frameWidth;
                sprite->default_frame_height = frameHeight;

                // Optional 9-slice tail (1 byte flag + 4 * uint16) — chunk_size 17+
                if (chunk_size >= 17)
                {
                    const uint8_t* nine = metadata + offset + 8;
                    if (nine[0])
                    {
                        sprite->has_nine_slice    = true;
                        sprite->nine_slice_left   = *(uint16_t*)(nine + 1);
                        sprite->nine_slice_right  = *(uint16_t*)(nine + 3);
                        sprite->nine_slice_top    = *(uint16_t*)(nine + 5);
                        sprite->nine_slice_bottom = *(uint16_t*)(nine + 7);
                    }
                }

                DEKI_LOG_INTERNAL("  Sprite metadata: frame %dx%d, 9-slice=%d",
                                  frameWidth, frameHeight, sprite->has_nine_slice ? 1 : 0);
            }
            else if (chunk_type == 2 && chunk_size >= 2)  // Frame list chunk
            {
                uint16_t frameCount = *(uint16_t*)(metadata + offset);
                uint32_t frame_offset = sizeof(uint16_t);

                // Each frame: 36 bytes GUID + 4*int32_t (x,y,w,h) = 52 bytes
                const uint32_t FRAME_ENTRY_SIZE = 36 + 4 * sizeof(int32_t);
                uint32_t expected_size = sizeof(uint16_t) + frameCount * FRAME_ENTRY_SIZE;

                if (chunk_size >= expected_size)
                {
                    sprite->frames.resize(frameCount);
                    for (uint16_t fi = 0; fi < frameCount; ++fi)
                    {
                        SpriteFrame& frame = sprite->frames[fi];
                        // Read GUID (36 chars)
                        memcpy(frame.guid, metadata + offset + frame_offset, 36);
                        frame.guid[36] = '\0';
                        frame_offset += 36;
                        // Read coordinates
                        frame.x = *(int32_t*)(metadata + offset + frame_offset);
                        frame_offset += sizeof(int32_t);
                        frame.y = *(int32_t*)(metadata + offset + frame_offset);
                        frame_offset += sizeof(int32_t);
                        frame.width = *(int32_t*)(metadata + offset + frame_offset);
                        frame_offset += sizeof(int32_t);
                        frame.height = *(int32_t*)(metadata + offset + frame_offset);
                        frame_offset += sizeof(int32_t);
                    }
                    DEKI_LOG_INTERNAL("  Frame list: %u frames", frameCount);
                }
            }
            // Skip to next chunk
            offset += chunk_size;
        }

        DekiMemoryProvider::Free(metadata);
    }
    else if (metadata)
    {
        DekiMemoryProvider::Free(metadata);
    }

    uint32_t tReadDone = DekiTime::GetTime();

    // Pixel data is now owned by sprite
    sprite->data = pixel_data;
#ifdef DEKI_EDITOR
    sprite->allocated_with_backend = true;  // Allocated with DekiMemoryProvider in play mode
#endif

    // For RGB565A8 sprites marked as having alpha, check if all pixels are actually opaque.
    // If so, clear has_alpha so QuadBlit can use the fast memcpy path instead of per-pixel blending.
    if (sprite->has_alpha && sprite->format == Texture2D::TextureFormat::RGB565A8)
    {
        // If exporter already determined all pixels are opaque, skip the scan
        if (header.flags & DTEX_FLAG_ALL_OPAQUE)
        {
            sprite->has_alpha = false;
        }
        else
        {
            bool allOpaque = true;
            int32_t w = sprite->width;
            int32_t h = sprite->height;

            // Scan for any non-opaque pixel
            for (int32_t i = 0; i < w * h; i++)
            {
                if (pixel_data[i * 3 + 2] != 255)
                {
                    allOpaque = false;
                    break;
                }
            }

            if (allOpaque)
            {
                sprite->has_alpha = false;
            }
            else
            {
                // Build per-row opaque span data for fast blitting.
                sprite->alphaRowSpans = new int16_t[h * 2];
                for (int32_t y = 0; y < h; y++)
                {
                    const uint8_t* row = pixel_data + y * w * 3;
                    int16_t opaqueStart = (int16_t)w;
                    int16_t opaqueEnd = 0;

                    for (int32_t x = 0; x < w; x++)
                    {
                        if (row[x * 3 + 2] == 255)
                        {
                            opaqueStart = (int16_t)x;
                            break;
                        }
                    }

                    for (int32_t x = w - 1; x >= opaqueStart; x--)
                    {
                        if (row[x * 3 + 2] == 255)
                        {
                            opaqueEnd = (int16_t)(x + 1);
                            break;
                        }
                    }

                    sprite->alphaRowSpans[y * 2] = opaqueStart;
                    sprite->alphaRowSpans[y * 2 + 1] = opaqueEnd;
                }
            }
        }
    }

    uint32_t tEnd = DekiTime::GetTime();
    DEKI_LOG_INFO("[PERF] Sprite::Load: read=%ums, alpha=%ums, total=%ums (%dx%d) %s",
              tReadDone - tStart, tEnd - tReadDone, tEnd - tStart,
              sprite->width, sprite->height, file_path);

    DEKI_LOG_INTERNAL("Loaded sprite: %s (%dx%d, %s, pivot: %.2f,%.2f)",
              file_path,
              sprite->width,
              sprite->height,
              Texture2D::GetFormatName(sprite->format),
              sprite->pivot_x,
              sprite->pivot_y);

    return sprite;
}

Sprite* Sprite::LoadFromFileData(const uint8_t* fileData, size_t fileSize)
{
    if (!fileData || fileSize < sizeof(Texture2D::Header))
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: invalid data");
        return nullptr;
    }

    // Parse header from buffer
    Texture2D::Header header;
    memcpy(&header, fileData, sizeof(Texture2D::Header));

    if (!Texture2D::ValidateHeader(header))
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: invalid header");
        return nullptr;
    }

    size_t expected_size = sizeof(Texture2D::Header) + header.data_size + header.metadata_size;
    if (fileSize < expected_size)
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: file size mismatch");
        return nullptr;
    }

    const uint8_t* src = fileData + sizeof(Texture2D::Header);

    // Copy pixel data into PSRAM (sprite takes ownership)
    uint8_t* pixel_data = (uint8_t*)DekiMemoryProvider::Allocate(
        header.data_size, true, "Sprite::LoadFromFileData");
    if (!pixel_data)
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: alloc failed");
        return nullptr;
    }
    memcpy(pixel_data, src, header.data_size);
    src += header.data_size;

    // Create sprite
    Sprite* sprite = new Sprite();
    if (!sprite->LoadFromMemory(header, pixel_data))
    {
        DekiMemoryProvider::Free(pixel_data);
        delete sprite;
        return nullptr;
    }

    // Process metadata (same logic as Load)
    if (header.metadata_size > 0)
    {
        const uint8_t* metadata = src;
        uint32_t offset = 0;

        if (header.metadata_size >= sizeof(uint32_t))
        {
            uint32_t num_chunks = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);

            for (uint32_t i = 0; i < num_chunks && offset + 8 <= header.metadata_size; ++i)
            {
                uint32_t chunk_type = *(uint32_t*)(metadata + offset);
                offset += sizeof(uint32_t);
                uint32_t chunk_size = *(uint32_t*)(metadata + offset);
                offset += sizeof(uint32_t);

                if (offset + chunk_size > header.metadata_size) break;

                if (chunk_type == 1 && chunk_size >= 8)
                {
                    sprite->default_frame_width = *(int32_t*)(metadata + offset);
                    sprite->default_frame_height = *(int32_t*)(metadata + offset + sizeof(int32_t));

                    if (chunk_size >= 17)
                    {
                        const uint8_t* nine = metadata + offset + 8;
                        if (nine[0])
                        {
                            sprite->has_nine_slice    = true;
                            sprite->nine_slice_left   = *(uint16_t*)(nine + 1);
                            sprite->nine_slice_right  = *(uint16_t*)(nine + 3);
                            sprite->nine_slice_top    = *(uint16_t*)(nine + 5);
                            sprite->nine_slice_bottom = *(uint16_t*)(nine + 7);
                        }
                    }
                }
                else if (chunk_type == 2 && chunk_size >= 2)
                {
                    uint16_t frameCount = *(uint16_t*)(metadata + offset);
                    uint32_t frame_offset = sizeof(uint16_t);
                    const uint32_t FRAME_ENTRY_SIZE = 36 + 4 * sizeof(int32_t);

                    if (chunk_size >= sizeof(uint16_t) + frameCount * FRAME_ENTRY_SIZE)
                    {
                        sprite->frames.resize(frameCount);
                        for (uint16_t fi = 0; fi < frameCount; ++fi)
                        {
                            SpriteFrame& frame = sprite->frames[fi];
                            memcpy(frame.guid, metadata + offset + frame_offset, 36);
                            frame.guid[36] = '\0';
                            frame_offset += 36;
                            frame.x = *(int32_t*)(metadata + offset + frame_offset); frame_offset += sizeof(int32_t);
                            frame.y = *(int32_t*)(metadata + offset + frame_offset); frame_offset += sizeof(int32_t);
                            frame.width = *(int32_t*)(metadata + offset + frame_offset); frame_offset += sizeof(int32_t);
                            frame.height = *(int32_t*)(metadata + offset + frame_offset); frame_offset += sizeof(int32_t);
                        }
                    }
                }
                offset += chunk_size;
            }
        }
    }

    // Own pixel data
    sprite->data = pixel_data;
#ifdef DEKI_EDITOR
    sprite->allocated_with_backend = true;
#endif

    // Alpha scan (same as Load)
    if (sprite->has_alpha && sprite->format == Texture2D::TextureFormat::RGB565A8)
    {
        if (header.flags & DTEX_FLAG_ALL_OPAQUE)
        {
            sprite->has_alpha = false;
        }
        else
        {
            bool allOpaque = true;
            int32_t w = sprite->width;
            int32_t h = sprite->height;

            for (int32_t i = 0; i < w * h; i++)
            {
                if (pixel_data[i * 3 + 2] != 255) { allOpaque = false; break; }
            }

            if (allOpaque)
            {
                sprite->has_alpha = false;
            }
            else
            {
                sprite->alphaRowSpans = new int16_t[h * 2];
                for (int32_t y = 0; y < h; y++)
                {
                    const uint8_t* row = pixel_data + y * w * 3;
                    int16_t opaqueStart = (int16_t)w, opaqueEnd = 0;
                    for (int32_t x = 0; x < w; x++)
                        if (row[x * 3 + 2] == 255) { opaqueStart = (int16_t)x; break; }
                    for (int32_t x = w - 1; x >= opaqueStart; x--)
                        if (row[x * 3 + 2] == 255) { opaqueEnd = (int16_t)(x + 1); break; }
                    sprite->alphaRowSpans[y * 2] = opaqueStart;
                    sprite->alphaRowSpans[y * 2 + 1] = opaqueEnd;
                }
            }
        }
    }

    DEKI_LOG_INFO("[PERF] Sprite::LoadFromFileData: %dx%d (%zu bytes, from pack)",
                  sprite->width, sprite->height, fileSize);
    return sprite;
}

bool Sprite::LoadFromMemory(const Texture2D::Header& header, const uint8_t* pixel_data)
{
    // Call base class implementation
    if (!Texture2D::LoadFromMemory(header, pixel_data))
    {
        return false;
    }

    // Sprite-specific initialization already done in constructor
    return true;
}

Sprite* Sprite::CreateSolid(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b)
{
    DEKI_LOG_INTERNAL("Sprite::CreateSolid called: %dx%d, RGB(%d,%d,%d)", width, height, r, g, b);

    if (width <= 0 || height <= 0)
    {
        DEKI_LOG_ERROR("Sprite::CreateSolid - Invalid dimensions: %dx%d", width, height);
        return nullptr;
    }

    Sprite* sprite = new Sprite();
    sprite->width = width;
    sprite->height = height;
    sprite->format = Texture2D::TextureFormat::RGB565;  // Use RGB565 directly - native format!
    sprite->has_transparency = false;
    sprite->has_alpha = false;

    size_t data_size = width * height * 2;  // RGB565 format = 2 bytes per pixel
    DEKI_LOG_INTERNAL("Sprite::CreateSolid - Allocating %zu bytes for RGB565 data", data_size);

    sprite->data = (uint8_t*)DekiMemoryProvider::Allocate(
        data_size, true, "Sprite::CreateSolid");

    if (!sprite->data)
    {
        DEKI_LOG_ERROR("Sprite::CreateSolid - Failed to allocate memory!");
        delete sprite;
        return nullptr;
    }

    DEKI_LOG_INTERNAL("Sprite::CreateSolid - Memory allocated, filling with color...");

    // Convert RGB888 to RGB565
    uint16_t r5 = (r * 31) / 255;  // 5 bits for red
    uint16_t g6 = (g * 63) / 255;  // 6 bits for green
    uint16_t b5 = (b * 31) / 255;  // 5 bits for blue
    uint16_t rgb565 = (r5 << 11) | (g6 << 5) | b5;

    // Fill sprite with solid RGB565 color - much more efficient!
    uint16_t* data16 = (uint16_t*)sprite->data;
    for (int32_t i = 0; i < width * height; i++)
    {
        data16[i] = rgb565;
    }

    DEKI_LOG_INTERNAL("Sprite::CreateSolid - Sprite created successfully at %p", sprite);
    return sprite;
}

Sprite* Sprite::CreateSolidRGBA(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (width <= 0 || height <= 0) return nullptr;

    Sprite* sprite = new Sprite();
    sprite->width = width;
    sprite->height = height;
    sprite->format = Texture2D::TextureFormat::RGB565A8;
    sprite->has_transparency = false;
    sprite->has_alpha = true;

        size_t data_size = width * height * 3;  // RGB565A8 format (2 bytes RGB565 + 1 byte alpha)

    sprite->data = (uint8_t*)DekiMemoryProvider::Allocate(
        data_size, true, "Sprite::CreateSolidRGBA");

    if (!sprite->data)
    {
        delete sprite;
        return nullptr;
    }

    // Convert RGB888 to RGB565
    uint16_t r5 = (r * 31) / 255;  // 5 bits for red
    uint16_t g6 = (g * 63) / 255;  // 6 bits for green
    uint16_t b5 = (b * 31) / 255;  // 5 bits for blue
    uint16_t rgb565 = (r5 << 11) | (g6 << 5) | b5;

    // Fill sprite with solid color and alpha
    for (int32_t i = 0; i < width * height; i++)
    {
        size_t byte_index = i * 3;
        *(uint16_t*)(sprite->data + byte_index) = rgb565;  // RGB565
        sprite->data[byte_index + 2] = a;  // Alpha
    }

    return sprite;
}

void Sprite::BakeTiledInto(uint8_t* dst, int32_t dst_w, int32_t dst_h, const Sprite* source)
{
    uint32_t bytes_per_pixel = Texture2D::GetBytesPerPixel(source->format);

    for (int32_t y = 0; y < dst_h; ++y)
    {
        int32_t src_y = y % source->height;
        for (int32_t x = 0; x < dst_w; ++x)
        {
            int32_t src_x = x % source->width;

            int32_t dst_idx = (y * dst_w + x) * bytes_per_pixel;
            int32_t src_idx = (src_y * source->width + src_x) * bytes_per_pixel;

            for (uint32_t i = 0; i < bytes_per_pixel; ++i)
            {
                dst[dst_idx + i] = source->data[src_idx + i];
            }
        }
    }
}

Sprite* Sprite::CreateTiled(Sprite* source, int32_t target_width, int32_t target_height)
{
    if (!source || !source->data || source->width <= 0 || source->height <= 0 || target_width <= 0 ||
        target_height <= 0)
    {
        return nullptr;
    }

    Sprite* tiled = new Sprite();
    tiled->width = target_width;
    tiled->height = target_height;
    tiled->format = source->format;
    tiled->has_transparency = source->has_transparency;
    tiled->has_alpha = source->has_alpha;

    // Copy sprite-specific properties
    tiled->pivot_x = source->pivot_x;
    tiled->pivot_y = source->pivot_y;
    tiled->pixels_per_unit = source->pixels_per_unit;
    tiled->transparent_r = source->transparent_r;
    tiled->transparent_g = source->transparent_g;
    tiled->transparent_b = source->transparent_b;

    uint32_t bytes_per_pixel = Texture2D::GetBytesPerPixel(source->format);
    size_t tiled_data_size = target_width * target_height * bytes_per_pixel;
    tiled->data = (uint8_t*)DekiMemoryProvider::Allocate(tiled_data_size, true);

    if (!tiled->data)
    {
        delete tiled;
        return nullptr;
    }

    BakeTiledInto(tiled->data, target_width, target_height, source);
    return tiled;
}

// 9-slice implementation

bool Sprite::SetNineSliceBorders(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom)
{
    // Validate that borders don't exceed sprite dimensions
    if (left + right >= width || top + bottom >= height)
    {
        DEKI_LOG_ERROR("Invalid 9-slice borders: L=%u R=%u T=%u B=%u for sprite %dx%d",
                      left, right, top, bottom, width, height);
        return false;
    }

    nine_slice_left = left;
    nine_slice_right = right;
    nine_slice_top = top;
    nine_slice_bottom = bottom;
    has_nine_slice = true;

    DEKI_LOG_INTERNAL("Set 9-slice borders: L=%u R=%u T=%u B=%u", left, right, top, bottom);
    return true;
}

void Sprite::BakeNineSliceInto(uint8_t* dst, int32_t target_width, int32_t target_height, const Sprite* source)
{
    uint32_t bytes_per_pixel = Texture2D::GetBytesPerPixel(source->format);

    // Calculate region dimensions
    // Source regions
    int32_t src_left = source->nine_slice_left;
    int32_t src_right = source->nine_slice_right;
    int32_t src_top = source->nine_slice_top;
    int32_t src_bottom = source->nine_slice_bottom;
    int32_t src_center_w = source->width - src_left - src_right;
    int32_t src_center_h = source->height - src_top - src_bottom;

    // Destination regions
    int32_t dst_left = src_left;
    int32_t dst_right = src_right;
    int32_t dst_top = src_top;
    int32_t dst_bottom = src_bottom;
    int32_t dst_center_w = target_width - dst_left - dst_right;
    int32_t dst_center_h = target_height - dst_top - dst_bottom;

    // Helper lambda to copy a pixel region with nearest-neighbor scaling
    auto CopyRegion = [](uint8_t* dst, int32_t dst_width, int32_t dst_x, int32_t dst_y,
                         int32_t dst_w, int32_t dst_h,
                         const uint8_t* src, int32_t src_width, int32_t src_x, int32_t src_y,
                         int32_t src_w, int32_t src_h, uint32_t bytes_per_pixel)
    {
        for (int32_t dy = 0; dy < dst_h; dy++)
        {
            // Calculate source Y using nearest-neighbor
            int32_t sy = (dy * src_h) / dst_h;
            const uint8_t* src_row = src + ((src_y + sy) * src_width + src_x) * bytes_per_pixel;
            uint8_t* dst_row = dst + ((dst_y + dy) * dst_width + dst_x) * bytes_per_pixel;

            for (int32_t dx = 0; dx < dst_w; dx++)
            {
                // Calculate source X using nearest-neighbor
                int32_t sx = (dx * src_w) / dst_w;
                const uint8_t* src_pixel = src_row + sx * bytes_per_pixel;
                uint8_t* dst_pixel = dst_row + dx * bytes_per_pixel;

                // Copy pixel data
                for (uint32_t b = 0; b < bytes_per_pixel; b++)
                {
                    dst_pixel[b] = src_pixel[b];
                }
            }
        }
    };

    // Process all 9 regions:
    // +----+--------+----+
    // | TL |  Top   | TR |
    // +----+--------+----+
    // | L  | Center | R  |
    // +----+--------+----+
    // | BL | Bottom | BR |
    // +----+--------+----+

    // Top-left corner (copy as-is)
    if (src_left > 0 && src_top > 0)
    {
        CopyRegion(dst, target_width, 0, 0, dst_left, dst_top,
                  source->data, source->width, 0, 0, src_left, src_top, bytes_per_pixel);
    }

    // Top edge (stretch horizontally)
    if (src_top > 0 && src_center_w > 0)
    {
        CopyRegion(dst, target_width, dst_left, 0, dst_center_w, dst_top,
                  source->data, source->width, src_left, 0, src_center_w, src_top, bytes_per_pixel);
    }

    // Top-right corner (copy as-is)
    if (src_right > 0 && src_top > 0)
    {
        CopyRegion(dst, target_width, target_width - dst_right, 0, dst_right, dst_top,
                  source->data, source->width, source->width - src_right, 0, src_right, src_top, bytes_per_pixel);
    }

    // Left edge (stretch vertically)
    if (src_left > 0 && src_center_h > 0)
    {
        CopyRegion(dst, target_width, 0, dst_top, dst_left, dst_center_h,
                  source->data, source->width, 0, src_top, src_left, src_center_h, bytes_per_pixel);
    }

    // Center (stretch both directions)
    if (src_center_w > 0 && src_center_h > 0)
    {
        CopyRegion(dst, target_width, dst_left, dst_top, dst_center_w, dst_center_h,
                  source->data, source->width, src_left, src_top, src_center_w, src_center_h, bytes_per_pixel);
    }

    // Right edge (stretch vertically)
    if (src_right > 0 && src_center_h > 0)
    {
        CopyRegion(dst, target_width, target_width - dst_right, dst_top, dst_right, dst_center_h,
                  source->data, source->width, source->width - src_right, src_top, src_right, src_center_h, bytes_per_pixel);
    }

    // Bottom-left corner (copy as-is)
    if (src_left > 0 && src_bottom > 0)
    {
        CopyRegion(dst, target_width, 0, target_height - dst_bottom, dst_left, dst_bottom,
                  source->data, source->width, 0, source->height - src_bottom, src_left, src_bottom, bytes_per_pixel);
    }

    // Bottom edge (stretch horizontally)
    if (src_center_w > 0 && src_bottom > 0)
    {
        CopyRegion(dst, target_width, dst_left, target_height - dst_bottom, dst_center_w, dst_bottom,
                  source->data, source->width, src_left, source->height - src_bottom, src_center_w, src_bottom, bytes_per_pixel);
    }

    // Bottom-right corner (copy as-is)
    if (src_right > 0 && src_bottom > 0)
    {
        CopyRegion(dst, target_width, target_width - dst_right, target_height - dst_bottom, dst_right, dst_bottom,
                  source->data, source->width, source->width - src_right, source->height - src_bottom, src_right, src_bottom, bytes_per_pixel);
    }
}

Sprite* Sprite::CreateNineSlice(Sprite* source, int32_t target_width, int32_t target_height)
{
    // Validate input
    if (!source || !source->data)
    {
        DEKI_LOG_ERROR("CreateNineSlice: Invalid source sprite");
        return nullptr;
    }

    if (!source->has_nine_slice)
    {
        DEKI_LOG_ERROR("CreateNineSlice: Source sprite does not have 9-slice data");
        return nullptr;
    }

    // Validate target dimensions
    int32_t min_width = source->nine_slice_left + source->nine_slice_right;
    int32_t min_height = source->nine_slice_top + source->nine_slice_bottom;

    if (target_width < min_width || target_height < min_height)
    {
        DEKI_LOG_ERROR("CreateNineSlice: Target size %dx%d too small (min: %dx%d)",
                      target_width, target_height, min_width, min_height);
        return nullptr;
    }

    Sprite* result = new Sprite();
    result->width = target_width;
    result->height = target_height;
    result->format = source->format;
    result->has_transparency = source->has_transparency;
    result->has_alpha = source->has_alpha;

    result->pivot_x = source->pivot_x;
    result->pivot_y = source->pivot_y;
    result->pixels_per_unit = source->pixels_per_unit;
    result->transparent_r = source->transparent_r;
    result->transparent_g = source->transparent_g;
    result->transparent_b = source->transparent_b;

    result->has_nine_slice = source->has_nine_slice;
    result->nine_slice_left = source->nine_slice_left;
    result->nine_slice_right = source->nine_slice_right;
    result->nine_slice_top = source->nine_slice_top;
    result->nine_slice_bottom = source->nine_slice_bottom;

    uint32_t bytes_per_pixel = Texture2D::GetBytesPerPixel(source->format);
    size_t result_data_size = target_width * target_height * bytes_per_pixel;
    result->data = (uint8_t*)DekiMemoryProvider::Allocate(result_data_size, true, "Sprite::CreateNineSlice");

    if (!result->data)
    {
        DEKI_LOG_ERROR("CreateNineSlice: Failed to allocate memory for scaled sprite");
        delete result;
        return nullptr;
    }

    BakeNineSliceInto(result->data, target_width, target_height, source);

    DEKI_LOG_INTERNAL("Created 9-slice sprite: %dx%d -> %dx%d",
                  source->width, source->height, target_width, target_height);

    return result;
}

// Self-register sprite loader with AssetManager
namespace {
    struct _SpriteLoaderReg {
        _SpriteLoaderReg() {
            Deki::AssetManager::RegisterLoader("Sprite",
                [](const char* p) -> void* {
                    auto* s = Sprite::Load(p);
                    if (s) DekiTime::Delay(1); // Yield for watchdog on embedded
                    return s;
                },
                [](void* a) { delete static_cast<Sprite*>(a); },
                [](const uint8_t* d, size_t s) -> void* { return Sprite::LoadFromFileData(d, s); });
            // Also register as "Texture" (alias)
            Deki::AssetManager::RegisterLoader("Texture",
                [](const char* p) -> void* {
                    auto* s = Sprite::Load(p);
                    if (s) DekiTime::Delay(1);
                    return s;
                },
                [](void* a) { delete static_cast<Sprite*>(a); },
                [](const uint8_t* d, size_t s) -> void* { return Sprite::LoadFromFileData(d, s); });
        }
    };
    static _SpriteLoaderReg s_spriteLoaderReg;
}
