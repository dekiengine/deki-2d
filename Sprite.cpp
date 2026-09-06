#include "Sprite.h"
#include "Texture2D.h"

#include <cstdlib>
#include <cstring>

#include <deki/providers/FileSystem.h>
#include <deki/providers/Memory.h>
#include <deki/LogSystem.h>
#include <deki/Time.h>
#include <deki/assets/AssetManager.h>

// Per-row opaque spans for RGB565A8 pixels: [start, end) of the longest run
// of fully opaque pixels in each row. The blitter copies that run straight
// and blends everything outside it per pixel, so the run must not contain a
// soft or transparent pixel (first-to-last opaque used to be recorded, and
// the notch of a heart or the gap between two legs came out in whatever
// colour the transparent pixel carried). A row with no opaque pixel gets an
// EMPTY span at the row end, so the blitter's left region covers the row
// once and its right region is empty. (start=w, end=0 made both regions
// cover the whole row: every soft pixel blended twice, and the right loop
// started at x=0 regardless of the clip rect.)
static void BuildOpaqueRowSpans(const uint8_t* pixel_data, int32_t w, int32_t h, int16_t* spans)
{
    for (int32_t y = 0; y < h; y++)
    {
        const uint8_t* row = pixel_data + y * w * 3;
        int32_t bestStart = w, bestEnd = w;
        int32_t runStart = -1;
        for (int32_t x = 0; x <= w; x++)
        {
            const bool opaque = (x < w) && row[x * 3 + 2] == 255;
            if (opaque && runStart < 0)
                runStart = x;
            if (!opaque && runStart >= 0)
            {
                if (x - runStart > bestEnd - bestStart || bestStart >= w)
                {
                    bestStart = runStart;
                    bestEnd = x;
                }
                runStart = -1;
            }
        }
        spans[y * 2] = (int16_t)bestStart;
        spans[y * 2 + 1] = (int16_t)bestEnd;
    }
}

Sprite::Sprite() : Texture2D()
{
    SetDefaultSpriteProperties();
}

Sprite::~Sprite()
{
    // Base class destructor handles pixel data cleanup
    if (chromaRowSpans)
    {
        delete[] chromaRowSpans;
        chromaRowSpans = nullptr;
    }
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
    pivotX = 0.5f;  // Center pivot
    pivotY = 0.5f;  // Center pivot
    // Default 16 to match the project's default pixelsPerMeter. This value
    // is *ignored* in Pixels mode (Standard2DRenderer overrides to 1.0 — see
    // its drawScale block) so backward-compat for legacy/Pixel-mode projects
    // is preserved. In Meters mode, it makes a 16-px sprite occupy 1 m × 1 m
    // by default (the natural mental model for retro tile workflows).
    pixelsPerMeter = 16.0f;
    transparentR = 255;  // Magenta as default transparent color
    transparentG = 0;
    transparentB = 255;
    hasChromaKey = false;
    chromaRowSpans = nullptr;

    // 9-slice defaults
    hasNineSlice = false;
    nineSliceLeft = 0;
    nineSliceRight = 0;
    nineSliceTop = 0;
    nineSliceBottom = 0;

    // Spritesheet defaults
    defaultFrameWidth = 0;
    defaultFrameHeight = 0;
}

// Loading functions - same for simulator and editor
Sprite* Sprite::Load(const char* file_path)
{
    if (!file_path)
    {
        DEKI_LOG_ERROR("NULL file path");
        return nullptr;
    }

    Deki::IFileSystem* fs = Deki::FileSystem::GetFileSystemForPath(file_path);
    if (!fs) {
        DEKI_LOG_INTERNAL("FileSystem not initialized for path: %s", file_path);
        return nullptr;
    }

    // Open file
    Deki::IFileSystem::FileHandle file = fs->OpenFile(file_path, Deki::IFileSystem::OpenMode::READ_BINARY);
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
    size_t expected_size = sizeof(Texture2D::Header) + header.dataSize + header.metadataSize;
    if (file_size < expected_size)
    {
        DEKI_LOG_ERROR("File size mismatch. Expected: %zu, Got: %ld", expected_size, file_size);
        fs->CloseFile(file);
        return nullptr;
    }

    // Read pixel data
    uint8_t* pixel_data = (uint8_t*)Deki::Memory::Allocate(
        header.dataSize, true, "Sprite::Load");

    if (!pixel_data)
    {
        DEKI_LOG_ERROR("Failed to allocate memory for sprite data");
        fs->CloseFile(file);
        return nullptr;
    }

    bytes_read = fs->ReadFile(file, pixel_data, header.dataSize);
    if (bytes_read != header.dataSize)
    {
        DEKI_LOG_ERROR("Failed to read sprite pixel data");
        Deki::Memory::Free(pixel_data);
        fs->CloseFile(file);
        return nullptr;
    }

    // Read metadata if present
    uint8_t* metadata = nullptr;
    if (header.metadataSize > 0)
    {
        metadata = (uint8_t*)Deki::Memory::Allocate(
            header.metadataSize, false, "Sprite::Load-metadata");

        if (metadata)
        {
            bytes_read = fs->ReadFile(file, metadata, header.metadataSize);
            if (bytes_read != header.metadataSize)
            {
                DEKI_LOG_WARNING("Failed to read sprite metadata, using defaults");
                Deki::Memory::Free(metadata);
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
        Deki::Memory::Free(pixel_data);
        if (metadata) Deki::Memory::Free(metadata);
        delete sprite;
        return nullptr;
    }

    // Process metadata (chunked format for 2DTX)
    if (metadata && header.metadataSize >= sizeof(uint32_t))
    {
        // Parse chunked metadata
        uint32_t offset = 0;
        uint32_t num_chunks = *(uint32_t*)(metadata + offset);
        offset += sizeof(uint32_t);

        for (uint32_t i = 0; i < num_chunks && offset + 8 <= header.metadataSize; ++i)
        {
            uint32_t chunk_type = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);
            uint32_t chunk_size = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);

            if (offset + chunk_size > header.metadataSize)
                break;  // Corrupted metadata

            if (chunk_type == 1 && chunk_size >= 8)  // Sprite chunk
            {
                int32_t frameWidth = *(int32_t*)(metadata + offset);
                int32_t frameHeight = *(int32_t*)(metadata + offset + sizeof(int32_t));
                sprite->defaultFrameWidth = frameWidth;
                sprite->defaultFrameHeight = frameHeight;

                // Optional 9-slice tail (1 byte flag + 4 * uint16) — chunk_size 17+
                if (chunk_size >= 17)
                {
                    const uint8_t* nine = metadata + offset + 8;
                    if (nine[0])
                    {
                        sprite->hasNineSlice    = true;
                        sprite->nineSliceLeft   = *(uint16_t*)(nine + 1);
                        sprite->nineSliceRight  = *(uint16_t*)(nine + 3);
                        sprite->nineSliceTop    = *(uint16_t*)(nine + 5);
                        sprite->nineSliceBottom = *(uint16_t*)(nine + 7);
                    }
                }

                DEKI_LOG_INTERNAL("  Sprite metadata: frame %dx%d, 9-slice=%d",
                                  frameWidth, frameHeight, sprite->hasNineSlice ? 1 : 0);
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
            else if (chunk_type == 3 && chunk_size >= 8)  // Chroma key chunk
            {
                // Layout: enabled(u8), r(u8), g(u8), b(u8), row_spans_count(u32),
                //         int16[row_spans_count] spans
                const uint8_t* p = metadata + offset;
                uint8_t enabled = p[0];
                if (enabled)
                {
                    sprite->hasChromaKey = true;
                    sprite->transparentR = p[1];
                    sprite->transparentG = p[2];
                    sprite->transparentB = p[3];
                }
                uint32_t spansCount = *(const uint32_t*)(p + 4);
                if (enabled && spansCount > 0 &&
                    chunk_size >= 8 + spansCount * sizeof(int16_t))
                {
                    sprite->chromaRowSpans = new int16_t[spansCount];
                    memcpy(sprite->chromaRowSpans, p + 8, spansCount * sizeof(int16_t));
                }
                DEKI_LOG_INTERNAL("  Chroma key: enabled=%d rgb=(%u,%u,%u) spans=%u",
                                  (int)enabled, p[1], p[2], p[3], spansCount);
            }
            // Skip to next chunk
            offset += chunk_size;
        }

        Deki::Memory::Free(metadata);
    }
    else if (metadata)
    {
        Deki::Memory::Free(metadata);
    }

    // Pixel data is now owned by sprite
    sprite->data = pixel_data;
#ifdef DEKI_EDITOR
    sprite->allocatedWithBackend = true;  // Allocated with Deki::Memory in play mode
#endif

    // For RGB565A8 sprites marked as having alpha, check if all pixels are actually opaque.
    // If so, clear hasAlpha so QuadBlit can use the fast memcpy path instead of per-pixel blending.
    if (sprite->hasAlpha && sprite->format == Texture2D::TextureFormat::RGB565A8)
    {
        // If exporter already determined all pixels are opaque, skip the scan
        if (header.flags & DTEX_FLAG_ALL_OPAQUE)
        {
            sprite->hasAlpha = false;
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
                sprite->hasAlpha = false;
            }
            else
            {
                // Build per-row opaque span data for fast blitting.
                sprite->alphaRowSpans = new int16_t[h * 2];
                BuildOpaqueRowSpans(pixel_data, w, h, sprite->alphaRowSpans);
            }
        }
    }

    DEKI_LOG_INTERNAL("Loaded sprite: %s (%dx%d, %s, pivot: %.2f,%.2f)",
              file_path,
              sprite->width,
              sprite->height,
              Texture2D::GetFormatName(sprite->format),
              sprite->pivotX,
              sprite->pivotY);

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

    size_t expected_size = sizeof(Texture2D::Header) + header.dataSize + header.metadataSize;
    if (fileSize < expected_size)
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: file size mismatch");
        return nullptr;
    }

    const uint8_t* src = fileData + sizeof(Texture2D::Header);

    // Copy pixel data into PSRAM (sprite takes ownership)
    uint8_t* pixel_data = (uint8_t*)Deki::Memory::Allocate(
        header.dataSize, true, "Sprite::LoadFromFileData");
    if (!pixel_data)
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: alloc failed");
        return nullptr;
    }
    memcpy(pixel_data, src, header.dataSize);
    src += header.dataSize;

    // Create sprite
    Sprite* sprite = new Sprite();
    if (!sprite->LoadFromMemory(header, pixel_data))
    {
        Deki::Memory::Free(pixel_data);
        delete sprite;
        return nullptr;
    }

    // Process metadata (same logic as Load)
    if (header.metadataSize > 0)
    {
        const uint8_t* metadata = src;
        uint32_t offset = 0;

        if (header.metadataSize >= sizeof(uint32_t))
        {
            uint32_t num_chunks = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);

            for (uint32_t i = 0; i < num_chunks && offset + 8 <= header.metadataSize; ++i)
            {
                uint32_t chunk_type = *(uint32_t*)(metadata + offset);
                offset += sizeof(uint32_t);
                uint32_t chunk_size = *(uint32_t*)(metadata + offset);
                offset += sizeof(uint32_t);

                if (offset + chunk_size > header.metadataSize) break;

                if (chunk_type == 1 && chunk_size >= 8)
                {
                    sprite->defaultFrameWidth = *(int32_t*)(metadata + offset);
                    sprite->defaultFrameHeight = *(int32_t*)(metadata + offset + sizeof(int32_t));

                    if (chunk_size >= 17)
                    {
                        const uint8_t* nine = metadata + offset + 8;
                        if (nine[0])
                        {
                            sprite->hasNineSlice    = true;
                            sprite->nineSliceLeft   = *(uint16_t*)(nine + 1);
                            sprite->nineSliceRight  = *(uint16_t*)(nine + 3);
                            sprite->nineSliceTop    = *(uint16_t*)(nine + 5);
                            sprite->nineSliceBottom = *(uint16_t*)(nine + 7);
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
                else if (chunk_type == 3 && chunk_size >= 8)
                {
                    const uint8_t* p = metadata + offset;
                    uint8_t enabled = p[0];
                    if (enabled)
                    {
                        sprite->hasChromaKey = true;
                        sprite->transparentR = p[1];
                        sprite->transparentG = p[2];
                        sprite->transparentB = p[3];
                    }
                    uint32_t spansCount = *(const uint32_t*)(p + 4);
                    if (enabled && spansCount > 0 &&
                        chunk_size >= 8 + spansCount * sizeof(int16_t))
                    {
                        sprite->chromaRowSpans = new int16_t[spansCount];
                        memcpy(sprite->chromaRowSpans, p + 8, spansCount * sizeof(int16_t));
                    }
                }
                offset += chunk_size;
            }
        }
    }

    // Own pixel data
    sprite->data = pixel_data;
#ifdef DEKI_EDITOR
    sprite->allocatedWithBackend = true;
#endif

    // Alpha scan (same as Load)
    if (sprite->hasAlpha && sprite->format == Texture2D::TextureFormat::RGB565A8)
    {
        if (header.flags & DTEX_FLAG_ALL_OPAQUE)
        {
            sprite->hasAlpha = false;
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
                sprite->hasAlpha = false;
            }
            else
            {
                sprite->alphaRowSpans = new int16_t[h * 2];
                BuildOpaqueRowSpans(pixel_data, w, h, sprite->alphaRowSpans);
            }
        }
    }

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
    sprite->hasTransparency = false;
    sprite->hasAlpha = false;

    size_t dataSize = width * height * 2;  // RGB565 format = 2 bytes per pixel
    DEKI_LOG_INTERNAL("Sprite::CreateSolid - Allocating %zu bytes for RGB565 data", dataSize);

    sprite->data = (uint8_t*)Deki::Memory::Allocate(
        dataSize, true, "Sprite::CreateSolid");

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
    sprite->hasTransparency = false;
    sprite->hasAlpha = true;

        size_t dataSize = width * height * 3;  // RGB565A8 format (2 bytes RGB565 + 1 byte alpha)

    sprite->data = (uint8_t*)Deki::Memory::Allocate(
        dataSize, true, "Sprite::CreateSolidRGBA");

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
    tiled->hasTransparency = source->hasTransparency;
    tiled->hasAlpha = source->hasAlpha;

    // Copy sprite-specific properties
    tiled->pivotX = source->pivotX;
    tiled->pivotY = source->pivotY;
    tiled->pixelsPerMeter = source->pixelsPerMeter;
    tiled->transparentR = source->transparentR;
    tiled->transparentG = source->transparentG;
    tiled->transparentB = source->transparentB;
    tiled->hasChromaKey = source->hasChromaKey;
    // chromaRowSpans is intentionally NOT copied: tiled output has different
    // dimensions, so source spans don't apply. Render falls back to per-pixel
    // chroma compare for tiled sprites — uncommon and small perf cost.

    uint32_t bytes_per_pixel = Texture2D::GetBytesPerPixel(source->format);
    size_t tiled_data_size = target_width * target_height * bytes_per_pixel;
    tiled->data = (uint8_t*)Deki::Memory::Allocate(tiled_data_size, true);

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

    nineSliceLeft = left;
    nineSliceRight = right;
    nineSliceTop = top;
    nineSliceBottom = bottom;
    hasNineSlice = true;

    DEKI_LOG_INTERNAL("Set 9-slice borders: L=%u R=%u T=%u B=%u", left, right, top, bottom);
    return true;
}

void Sprite::BakeNineSliceInto(uint8_t* dst, int32_t target_width, int32_t target_height, const Sprite* source)
{
    uint32_t bytes_per_pixel = Texture2D::GetBytesPerPixel(source->format);

    // Calculate region dimensions
    // Source regions
    int32_t src_left = source->nineSliceLeft;
    int32_t src_right = source->nineSliceRight;
    int32_t src_top = source->nineSliceTop;
    int32_t src_bottom = source->nineSliceBottom;
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

    if (!source->hasNineSlice)
    {
        DEKI_LOG_ERROR("CreateNineSlice: Source sprite does not have 9-slice data");
        return nullptr;
    }

    // Validate target dimensions
    int32_t min_width = source->nineSliceLeft + source->nineSliceRight;
    int32_t min_height = source->nineSliceTop + source->nineSliceBottom;

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
    result->hasTransparency = source->hasTransparency;
    result->hasAlpha = source->hasAlpha;

    result->pivotX = source->pivotX;
    result->pivotY = source->pivotY;
    result->pixelsPerMeter = source->pixelsPerMeter;
    result->transparentR = source->transparentR;
    result->transparentG = source->transparentG;
    result->transparentB = source->transparentB;
    result->hasChromaKey = source->hasChromaKey;
    // chromaRowSpans not copied — see CreateTiled note.

    result->hasNineSlice = source->hasNineSlice;
    result->nineSliceLeft = source->nineSliceLeft;
    result->nineSliceRight = source->nineSliceRight;
    result->nineSliceTop = source->nineSliceTop;
    result->nineSliceBottom = source->nineSliceBottom;

    uint32_t bytes_per_pixel = Texture2D::GetBytesPerPixel(source->format);
    size_t result_data_size = target_width * target_height * bytes_per_pixel;
    result->data = (uint8_t*)Deki::Memory::Allocate(result_data_size, true, "Sprite::CreateNineSlice");

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
                    if (s) Deki::Time::Delay(1); // Yield for watchdog on embedded
                    return s;
                },
                [](void* a) { delete static_cast<Sprite*>(a); },
                [](const uint8_t* d, size_t s) -> void* { return Sprite::LoadFromFileData(d, s); });
            // Also register as "Texture" (alias)
            Deki::AssetManager::RegisterLoader("Texture",
                [](const char* p) -> void* {
                    auto* s = Sprite::Load(p);
                    if (s) Deki::Time::Delay(1);
                    return s;
                },
                [](void* a) { delete static_cast<Sprite*>(a); },
                [](const uint8_t* d, size_t s) -> void* { return Sprite::LoadFromFileData(d, s); });
        }
    };
    static _SpriteLoaderReg s_spriteLoaderReg;
}
