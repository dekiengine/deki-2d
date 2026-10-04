#include "Sprite.h"
#include "SpriteRowSpans.h"
#include <deki/assets/Texture2D.h>

#include <cstdlib>
#include <cstring>

#include <deki/providers/FileSystem.h>
#include <deki/providers/Memory.h>
#include <deki/LogSystem.h>
#include <deki/Time.h>
#include <deki/assets/AssetManager.h>

namespace Deki2D
{

Sprite::Sprite()
    : Deki::Texture2D()
{
    SetDefaultSpriteProperties();
}

Sprite::~Sprite()
{
    // The base class frees the pixels; chromaRowSpans frees itself.
}

namespace
{
// The frame list may end with the frames' own 9-slice borders: a u16 count,
// then per bordered frame u16 index, left, right, top, bottom. Files may stop
// after the entries, and loaders that do not know the tail ignore it.
void ReadFrameNineSlices(std::vector<SpriteFrame>& frames, const uint8_t* chunk, uint32_t chunkSize,
                         uint32_t entriesEnd)
{
    if (chunkSize < entriesEnd + sizeof(uint16_t))
    {
        return;
    }
    uint16_t count = 0;
    std::memcpy(&count, chunk + entriesEnd, sizeof(count));
    const uint32_t kEntry = 5 * sizeof(uint16_t);
    uint32_t at = entriesEnd + sizeof(uint16_t);
    for (uint16_t i = 0; i < count && at + kEntry <= chunkSize; ++i, at += kEntry)
    {
        uint16_t v[5];
        std::memcpy(v, chunk + at, sizeof(v));
        if (v[0] >= frames.size())
        {
            continue;
        }
        SpriteFrame& f = frames[v[0]];
        f.hasNineSlice = true;
        f.nineSliceLeft = v[1];
        f.nineSliceRight = v[2];
        f.nineSliceTop = v[3];
        f.nineSliceBottom = v[4];
    }
}
}  // namespace

const SpriteFrame* Sprite::FindFrame(const std::string& guid) const
{
    for (size_t i = 0; i < frames.size(); i++)
    {
        if (guid == frames[i].guid)
        {
            return &frames[i];
        }
    }
    return nullptr;
}

void Sprite::SetDefaultSpriteProperties()
{
    pivotX = 0.5f;  // Center pivot
    pivotY = 0.5f;  // Center pivot
    // The project's default pixels per meter, so a 16-px sprite is 1 m
    // across, which suits tile-based art.
    pixelsPerMeter = 16.0f;
    transparentR = 255;  // Magenta as default transparent color
    transparentG = 0;
    transparentB = 255;
    hasChromaKey = false;
    chromaRowSpans.Reset();

    hasNineSlice = false;
    nineSliceLeft = 0;
    nineSliceRight = 0;
    nineSliceTop = 0;
    nineSliceBottom = 0;

    defaultFrameWidth = 0;
    defaultFrameHeight = 0;

    sourceWidth = 0;
    sourceHeight = 0;
    sourceScale = 1.0f;
}

void Sprite::ApplySourceSize(int32_t imageWidth, int32_t imageHeight)
{
    if (imageWidth <= 0 || imageHeight <= 0 || (imageWidth == width && imageHeight == height))
    {
        return;
    }
    sourceWidth = imageWidth;
    sourceHeight = imageHeight;
    sourceScale = static_cast<float>(width) / static_cast<float>(imageWidth);

    // Rect edges, not sizes, so frames that touch in the image still touch.
    for (SpriteFrame& f : frames)
    {
        const int32_t x0 = SourceToStoredX(f.x), x1 = SourceToStoredX(f.x + f.width);
        const int32_t y0 = SourceToStoredY(f.y), y1 = SourceToStoredY(f.y + f.height);
        if (f.hasNineSlice)
        {
            f.nineSliceLeft = static_cast<uint16_t>(SourceToStoredX(f.x + f.nineSliceLeft) - x0);
            f.nineSliceRight = static_cast<uint16_t>(x1 - SourceToStoredX(f.x + f.width - f.nineSliceRight));
            f.nineSliceTop = static_cast<uint16_t>(SourceToStoredY(f.y + f.nineSliceTop) - y0);
            f.nineSliceBottom = static_cast<uint16_t>(y1 - SourceToStoredY(f.y + f.height - f.nineSliceBottom));
        }
        f.x = x0;
        f.y = y0;
        f.width = x1 > x0 ? x1 - x0 : 1;
        f.height = y1 > y0 ? y1 - y0 : 1;
    }
    if (defaultFrameWidth > 0)
    {
        defaultFrameWidth = SourceToStoredX(defaultFrameWidth) > 0 ? SourceToStoredX(defaultFrameWidth) : 1;
    }
    if (defaultFrameHeight > 0)
    {
        defaultFrameHeight = SourceToStoredY(defaultFrameHeight) > 0 ? SourceToStoredY(defaultFrameHeight) : 1;
    }
    if (hasNineSlice)
    {
        nineSliceLeft = static_cast<uint16_t>(SourceToStoredX(nineSliceLeft));
        nineSliceRight = static_cast<uint16_t>(width - SourceToStoredX(imageWidth - nineSliceRight));
        nineSliceTop = static_cast<uint16_t>(SourceToStoredY(nineSliceTop));
        nineSliceBottom = static_cast<uint16_t>(height - SourceToStoredY(imageHeight - nineSliceBottom));
    }
    // Fewer pixels for the same meters.
    pixelsPerMeter *= sourceScale;
}

// Loading, the same in the simulator and the editor.
Sprite* Sprite::Load(const char* filePath)
{
    if (!filePath)
    {
        DEKI_LOG_ERROR("NULL file path");
        return nullptr;
    }

    Deki::IFileSystem* fs = Deki::FileSystem::GetFileSystemForPath(filePath);
    if (!fs)
    {
        DEKI_LOG_INTERNAL("FileSystem not initialized for path: %s", filePath);
        return nullptr;
    }

    Deki::IFileSystem::FileHandle file = fs->OpenFile(filePath, Deki::IFileSystem::OpenMode::ReadBinary);
    if (!file)
    {
        DEKI_LOG_ERROR("Failed to open sprite file: %s", filePath);
        return nullptr;
    }

    long fileSize = fs->GetFileSize(file);
    if (fileSize < sizeof(Deki::Texture2D::Header))
    {
        DEKI_LOG_ERROR("File too small to contain texture header: %s", filePath);
        fs->CloseFile(file);
        return nullptr;
    }

    Deki::Texture2D::Header header;
    size_t bytesRead = fs->ReadFile(file, &header, sizeof(Deki::Texture2D::Header));
    if (bytesRead != sizeof(Deki::Texture2D::Header))
    {
        DEKI_LOG_ERROR("Failed to read sprite header: %s", filePath);
        fs->CloseFile(file);
        return nullptr;
    }

    if (!Deki::Texture2D::ValidateHeader(header))
    {
        DEKI_LOG_ERROR("Invalid sprite header: %s", filePath);
        fs->CloseFile(file);
        return nullptr;
    }

    // Other textures (font atlases, say) load through Sprite::Load() too.
    if (!(header.flags & DTEX_FLAG_IS_SPRITE))
    {
        DEKI_LOG_INTERNAL("Loading non-sprite texture as sprite: %s", filePath);
    }

    // Checked in 64 bits: on a 32-bit board a corrupt dataSize plus
    // metadataSize can wrap to a small number and pass.
    const uint64_t expectedSize =
        uint64_t(sizeof(Deki::Texture2D::Header)) + uint64_t(header.dataSize) + uint64_t(header.metadataSize);
    if (fileSize < 0 || uint64_t(fileSize) < expectedSize)
    {
        DEKI_LOG_ERROR("File size mismatch. Expected: %llu, Got: %ld", (unsigned long long)expectedSize, fileSize);
        fs->CloseFile(file);
        return nullptr;
    }

    uint8_t* pixelData = (uint8_t*)Deki::Memory::Allocate(header.dataSize, Deki::Memory::External);

    if (!pixelData)
    {
        DEKI_LOG_ERROR("Failed to allocate memory for sprite data");
        fs->CloseFile(file);
        return nullptr;
    }

    bytesRead = fs->ReadFile(file, pixelData, header.dataSize);
    if (bytesRead != header.dataSize)
    {
        DEKI_LOG_ERROR("Failed to read sprite pixel data");
        Deki::Memory::Free(pixelData);
        fs->CloseFile(file);
        return nullptr;
    }

    uint8_t* metadata = nullptr;
    if (header.metadataSize > 0)
    {
        metadata = (uint8_t*)Deki::Memory::Allocate(header.metadataSize, Deki::Memory::Internal);

        if (metadata)
        {
            bytesRead = fs->ReadFile(file, metadata, header.metadataSize);
            if (bytesRead != header.metadataSize)
            {
                DEKI_LOG_WARNING("Failed to read sprite metadata, using defaults");
                Deki::Memory::Free(metadata);
                metadata = nullptr;
            }
        }
    }

    fs->CloseFile(file);

    Sprite* sprite = new Sprite();
    if (!sprite->LoadFromMemory(header, pixelData))
    {
        DEKI_LOG_ERROR("Failed to load sprite from memory");
        Deki::Memory::Free(pixelData);
        if (metadata)
        {
            Deki::Memory::Free(metadata);
        }
        delete sprite;
        return nullptr;
    }

    // The metadata is a list of chunks.
    if (metadata && header.metadataSize >= sizeof(uint32_t))
    {
        uint32_t offset = 0;
        uint32_t numChunks = *(uint32_t*)(metadata + offset);
        offset += sizeof(uint32_t);
        int32_t imageWidth = 0, imageHeight = 0;  // SourceSize chunk, applied after the rest

        for (uint32_t i = 0; i < numChunks && offset + 8 <= header.metadataSize; ++i)
        {
            uint32_t chunkType = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);
            uint32_t chunkSize = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);

            // Written so it cannot wrap; offset + a huge chunkSize can, on 32 bits.
            if (chunkSize > header.metadataSize - offset)
            {
                break;  // Corrupted metadata
            }

            if (chunkType == 1 && chunkSize >= 8)  // Sprite chunk
            {
                int32_t frameWidth = *(int32_t*)(metadata + offset);
                int32_t frameHeight = *(int32_t*)(metadata + offset + sizeof(int32_t));
                sprite->defaultFrameWidth = frameWidth;
                sprite->defaultFrameHeight = frameHeight;

                // Optional 9-slice tail (1 byte flag + 4 * uint16) when chunkSize is 17 or more.
                if (chunkSize >= 17)
                {
                    const uint8_t* nine = metadata + offset + 8;
                    if (nine[0])
                    {
                        sprite->hasNineSlice = true;
                        sprite->nineSliceLeft = *(uint16_t*)(nine + 1);
                        sprite->nineSliceRight = *(uint16_t*)(nine + 3);
                        sprite->nineSliceTop = *(uint16_t*)(nine + 5);
                        sprite->nineSliceBottom = *(uint16_t*)(nine + 7);
                    }
                }

                DEKI_LOG_INTERNAL("  Sprite metadata: frame %dx%d, 9-slice=%d", frameWidth, frameHeight,
                                  sprite->hasNineSlice ? 1 : 0);
            }
            else if (chunkType == 2 && chunkSize >= 2)  // Frame list chunk
            {
                uint16_t frameCount = *(uint16_t*)(metadata + offset);
                uint32_t frameOffset = sizeof(uint16_t);

                // Each frame: 36 bytes GUID + 4*int32_t (x,y,w,h) = 52 bytes
                const uint32_t frameEntrySize = 36 + 4 * sizeof(int32_t);
                uint32_t expectedSize = sizeof(uint16_t) + frameCount * frameEntrySize;

                if (chunkSize >= expectedSize)
                {
                    sprite->frames.resize(frameCount);
                    for (uint16_t fi = 0; fi < frameCount; ++fi)
                    {
                        SpriteFrame& frame = sprite->frames[fi];
                        memcpy(frame.guid, metadata + offset + frameOffset, 36);
                        frame.guid[36] = '\0';
                        frameOffset += 36;
                        frame.x = *(int32_t*)(metadata + offset + frameOffset);
                        frameOffset += sizeof(int32_t);
                        frame.y = *(int32_t*)(metadata + offset + frameOffset);
                        frameOffset += sizeof(int32_t);
                        frame.width = *(int32_t*)(metadata + offset + frameOffset);
                        frameOffset += sizeof(int32_t);
                        frame.height = *(int32_t*)(metadata + offset + frameOffset);
                        frameOffset += sizeof(int32_t);
                    }
                    ReadFrameNineSlices(sprite->frames, metadata + offset, chunkSize, frameOffset);
                    DEKI_LOG_INTERNAL("  Frame list: %u frames", frameCount);
                }
            }
            else if (chunkType == 3 && chunkSize >= 8)  // Chroma key chunk
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
                // Divided, not multiplied: spansCount comes from the file, and
                // `spansCount * sizeof(int16_t)` is a 32-bit size_t on the device,
                // where a large count wraps to a small one and passes the check.
                const bool spansFit = chunkSize >= 8 && spansCount <= (chunkSize - 8) / sizeof(int16_t);
                if (enabled && spansCount > 0 && spansFit)
                {
                    sprite->chromaRowSpans.Allocate(spansCount, Deki::Memory::Internal);
                    if (sprite->chromaRowSpans)
                    {
                        memcpy(sprite->chromaRowSpans.Data(), p + 8, spansCount * sizeof(int16_t));
                    }
                    else
                    {
                        DEKI_LOG_WARNING("Sprite: no room for %u chroma spans; "
                                         "the slower per-pixel compare will be used",
                                         (unsigned)spansCount);
                    }
                }
                DEKI_LOG_INTERNAL("  Chroma key: enabled=%d rgb=(%u,%u,%u) spans=%u", (int)enabled, p[1], p[2], p[3],
                                  spansCount);
            }
            else if (chunkType == 4 && chunkSize >= 8)  // Source size (Max Size)
            {
                imageWidth = *(int32_t*)(metadata + offset);
                imageHeight = *(int32_t*)(metadata + offset + sizeof(int32_t));
            }
            offset += chunkSize;
        }
        sprite->ApplySourceSize(imageWidth, imageHeight);

        Deki::Memory::Free(metadata);
    }
    else if (metadata)
    {
        Deki::Memory::Free(metadata);
    }

    // The sprite owns the pixels now.
    sprite->data = pixelData;
#ifdef DEKI_EDITOR
    sprite->allocatedWithBackend = true;  // Allocated with Deki::Memory in play mode
#endif

    // An RGB565A8 sprite whose pixels are all opaque drops hasAlpha, so
    // QuadBlit copies rows with memcpy instead of blending each pixel.
    if (sprite->hasAlpha && sprite->format == Deki::Texture2D::TextureFormat::RGB565A8)
    {
        // The exporter may have checked already.
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
                if (pixelData[i * 3 + 2] != 255)
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
                // Opaque spans per row let the blitter copy those runs straight.
                sprite->alphaRowSpans.Allocate(static_cast<size_t>(h) * 2, Deki::Memory::Internal);
                if (sprite->alphaRowSpans)
                {
                    BuildOpaqueRowSpans(pixelData, w, h, sprite->alphaRowSpans.Data());
                }
                else
                {
                    DEKI_LOG_WARNING("Sprite: no room for %d alpha spans; the slower "
                                     "per-pixel path will be used",
                                     h * 2);
                }
            }
        }
    }

    DEKI_LOG_INTERNAL("Loaded sprite: %s (%dx%d, %s, pivot: %.2f,%.2f)", filePath, sprite->width, sprite->height,
                      Deki::Texture2D::GetFormatName(sprite->format), sprite->pivotX, sprite->pivotY);

    return sprite;
}

Sprite* Sprite::LoadFromFileData(const uint8_t* fileData, size_t fileSize)
{
    if (!fileData || fileSize < sizeof(Deki::Texture2D::Header))
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: invalid data");
        return nullptr;
    }

    Deki::Texture2D::Header header;
    memcpy(&header, fileData, sizeof(Deki::Texture2D::Header));

    if (!Deki::Texture2D::ValidateHeader(header))
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: invalid header");
        return nullptr;
    }

    const uint64_t expectedSize =
        uint64_t(sizeof(Deki::Texture2D::Header)) + uint64_t(header.dataSize) + uint64_t(header.metadataSize);
    if (uint64_t(fileSize) < expectedSize)
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: file size mismatch");
        return nullptr;
    }

    const uint8_t* src = fileData + sizeof(Deki::Texture2D::Header);

    // Pixels go to external memory (PSRAM); the sprite owns them.
    uint8_t* pixelData = (uint8_t*)Deki::Memory::Allocate(header.dataSize, Deki::Memory::External);
    if (!pixelData)
    {
        DEKI_LOG_ERROR("Sprite::LoadFromFileData: alloc failed");
        return nullptr;
    }
    memcpy(pixelData, src, header.dataSize);
    src += header.dataSize;

    Sprite* sprite = new Sprite();
    if (!sprite->LoadFromMemory(header, pixelData))
    {
        Deki::Memory::Free(pixelData);
        delete sprite;
        return nullptr;
    }

    // Metadata, as in Load.
    if (header.metadataSize > 0)
    {
        const uint8_t* metadata = src;
        uint32_t offset = 0;

        if (header.metadataSize >= sizeof(uint32_t))
        {
            uint32_t numChunks = *(uint32_t*)(metadata + offset);
            offset += sizeof(uint32_t);
            int32_t imageWidth = 0, imageHeight = 0;  // SourceSize chunk, applied after the rest

            for (uint32_t i = 0; i < numChunks && offset + 8 <= header.metadataSize; ++i)
            {
                uint32_t chunkType = *(uint32_t*)(metadata + offset);
                offset += sizeof(uint32_t);
                uint32_t chunkSize = *(uint32_t*)(metadata + offset);
                offset += sizeof(uint32_t);

                if (chunkSize > header.metadataSize - offset)
                {
                    break;  // cannot wrap, unlike offset + size
                }

                if (chunkType == 1 && chunkSize >= 8)
                {
                    sprite->defaultFrameWidth = *(int32_t*)(metadata + offset);
                    sprite->defaultFrameHeight = *(int32_t*)(metadata + offset + sizeof(int32_t));

                    if (chunkSize >= 17)
                    {
                        const uint8_t* nine = metadata + offset + 8;
                        if (nine[0])
                        {
                            sprite->hasNineSlice = true;
                            sprite->nineSliceLeft = *(uint16_t*)(nine + 1);
                            sprite->nineSliceRight = *(uint16_t*)(nine + 3);
                            sprite->nineSliceTop = *(uint16_t*)(nine + 5);
                            sprite->nineSliceBottom = *(uint16_t*)(nine + 7);
                        }
                    }
                }
                else if (chunkType == 2 && chunkSize >= 2)
                {
                    uint16_t frameCount = *(uint16_t*)(metadata + offset);
                    uint32_t frameOffset = sizeof(uint16_t);
                    const uint32_t frameEntrySize = 36 + 4 * sizeof(int32_t);

                    if (chunkSize >= sizeof(uint16_t) + frameCount * frameEntrySize)
                    {
                        sprite->frames.resize(frameCount);
                        for (uint16_t fi = 0; fi < frameCount; ++fi)
                        {
                            SpriteFrame& frame = sprite->frames[fi];
                            memcpy(frame.guid, metadata + offset + frameOffset, 36);
                            frame.guid[36] = '\0';
                            frameOffset += 36;
                            frame.x = *(int32_t*)(metadata + offset + frameOffset);
                            frameOffset += sizeof(int32_t);
                            frame.y = *(int32_t*)(metadata + offset + frameOffset);
                            frameOffset += sizeof(int32_t);
                            frame.width = *(int32_t*)(metadata + offset + frameOffset);
                            frameOffset += sizeof(int32_t);
                            frame.height = *(int32_t*)(metadata + offset + frameOffset);
                            frameOffset += sizeof(int32_t);
                        }
                        ReadFrameNineSlices(sprite->frames, metadata + offset, chunkSize, frameOffset);
                    }
                }
                else if (chunkType == 3 && chunkSize >= 8)
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
                    // Divided, not multiplied: spansCount comes from the file, and
                    // `spansCount * sizeof(int16_t)` is a 32-bit size_t on the device,
                    // where a large count wraps to a small one and passes the check.
                    const bool spansFit = chunkSize >= 8 && spansCount <= (chunkSize - 8) / sizeof(int16_t);
                    if (enabled && spansCount > 0 && spansFit)
                    {
                        sprite->chromaRowSpans.Allocate(spansCount, Deki::Memory::Internal);
                        if (sprite->chromaRowSpans)
                        {
                            memcpy(sprite->chromaRowSpans.Data(), p + 8, spansCount * sizeof(int16_t));
                        }
                        else
                        {
                            DEKI_LOG_WARNING("Sprite: no room for %u chroma spans; "
                                             "the slower per-pixel compare will be used",
                                             (unsigned)spansCount);
                        }
                    }
                }
                else if (chunkType == 4 && chunkSize >= 8)  // Source size (Max Size)
                {
                    imageWidth = *(int32_t*)(metadata + offset);
                    imageHeight = *(int32_t*)(metadata + offset + sizeof(int32_t));
                }
                offset += chunkSize;
            }
            sprite->ApplySourceSize(imageWidth, imageHeight);
        }
    }

    sprite->data = pixelData;
#ifdef DEKI_EDITOR
    sprite->allocatedWithBackend = true;
#endif

    // Alpha scan, as in Load.
    if (sprite->hasAlpha && sprite->format == Deki::Texture2D::TextureFormat::RGB565A8)
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
                if (pixelData[i * 3 + 2] != 255)
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
                sprite->alphaRowSpans.Allocate(static_cast<size_t>(h) * 2, Deki::Memory::Internal);
                if (sprite->alphaRowSpans)
                {
                    BuildOpaqueRowSpans(pixelData, w, h, sprite->alphaRowSpans.Data());
                }
                else
                {
                    DEKI_LOG_WARNING("Sprite: no room for %d alpha spans; the slower "
                                     "per-pixel path will be used",
                                     h * 2);
                }
            }
        }
    }

    return sprite;
}

bool Sprite::LoadFromMemory(const Deki::Texture2D::Header& header, const uint8_t* pixelData)
{
    if (!Deki::Texture2D::LoadFromMemory(header, pixelData))
    {
        return false;
    }

    // The constructor already set the sprite defaults.
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
    sprite->format = Deki::Texture2D::TextureFormat::RGB565;  // native format, no conversion
    sprite->hasTransparency = false;
    sprite->hasAlpha = false;

    size_t dataSize = width * height * 2;  // RGB565 format = 2 bytes per pixel
    DEKI_LOG_INTERNAL("Sprite::CreateSolid - Allocating %zu bytes for RGB565 data", dataSize);

    sprite->data = (uint8_t*)Deki::Memory::Allocate(dataSize, Deki::Memory::External);

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
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    Sprite* sprite = new Sprite();
    sprite->width = width;
    sprite->height = height;
    sprite->format = Deki::Texture2D::TextureFormat::RGB565A8;
    sprite->hasTransparency = false;
    sprite->hasAlpha = true;

    size_t dataSize = width * height * 3;  // RGB565A8 format (2 bytes RGB565 + 1 byte alpha)

    sprite->data = (uint8_t*)Deki::Memory::Allocate(dataSize, Deki::Memory::External);

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

    for (int32_t i = 0; i < width * height; i++)
    {
        size_t byteIndex = i * 3;
        *(uint16_t*)(sprite->data + byteIndex) = rgb565;  // RGB565
        sprite->data[byteIndex + 2] = a;                  // Alpha
    }

    return sprite;
}

void Sprite::BakeTiledInto(uint8_t* dst, int32_t dstW, int32_t dstH, const Sprite* source)
{
    SliceRegion all;
    all.width = source->width;
    all.height = source->height;
    BakeTiledRegion(dst, dstW, dstH, source, all);
}

void Sprite::BakeTiledRegion(uint8_t* dst, int32_t dstW, int32_t dstH, const Sprite* source, const SliceRegion& region)
{
    uint32_t bytesPerPixel = Deki::Texture2D::GetBytesPerPixel(source->format);

    for (int32_t y = 0; y < dstH; ++y)
    {
        int32_t srcY = region.y + y % region.height;
        for (int32_t x = 0; x < dstW; ++x)
        {
            int32_t srcX = region.x + x % region.width;

            int32_t dstIdx = (y * dstW + x) * bytesPerPixel;
            int32_t srcIdx = (srcY * source->width + srcX) * bytesPerPixel;

            for (uint32_t i = 0; i < bytesPerPixel; ++i)
            {
                dst[dstIdx + i] = source->data[srcIdx + i];
            }
        }
    }
}

Sprite* Sprite::CreateTiled(Sprite* source, int32_t targetWidth, int32_t targetHeight)
{
    if (!source || !source->data || source->width <= 0 || source->height <= 0 || targetWidth <= 0 || targetHeight <= 0)
    {
        return nullptr;
    }

    Sprite* tiled = new Sprite();
    tiled->width = targetWidth;
    tiled->height = targetHeight;
    tiled->format = source->format;
    tiled->hasTransparency = source->hasTransparency;
    tiled->hasAlpha = source->hasAlpha;

    tiled->pivotX = source->pivotX;
    tiled->pivotY = source->pivotY;
    tiled->pixelsPerMeter = source->pixelsPerMeter;
    tiled->transparentR = source->transparentR;
    tiled->transparentG = source->transparentG;
    tiled->transparentB = source->transparentB;
    tiled->hasChromaKey = source->hasChromaKey;
    // chromaRowSpans is not copied: the tiled sprite has other dimensions, so
    // the source spans do not apply. Tiled sprites use the per-pixel chroma
    // compare, a small cost for a rare case.

    uint32_t bytesPerPixel = Deki::Texture2D::GetBytesPerPixel(source->format);
    size_t tiledDataSize = targetWidth * targetHeight * bytesPerPixel;
    tiled->data = (uint8_t*)Deki::Memory::Allocate(tiledDataSize, Deki::Memory::External);

    if (!tiled->data)
    {
        delete tiled;
        return nullptr;
    }

    BakeTiledInto(tiled->data, targetWidth, targetHeight, source);
    return tiled;
}

// 9-slice implementation

bool Sprite::SetNineSliceBorders(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom)
{
    if (left + right >= width || top + bottom >= height)
    {
        DEKI_LOG_ERROR("Invalid 9-slice borders: L=%u R=%u T=%u B=%u for sprite %dx%d", left, right, top, bottom, width,
                       height);
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

void Sprite::BakeNineSliceInto(uint8_t* dst, int32_t targetWidth, int32_t targetHeight, const Sprite* source)
{
    SliceRegion all;
    all.width = source->width;
    all.height = source->height;
    all.left = source->nineSliceLeft;
    all.right = source->nineSliceRight;
    all.top = source->nineSliceTop;
    all.bottom = source->nineSliceBottom;
    BakeNineSliceRegion(dst, targetWidth, targetHeight, source, all);
}

void Sprite::BakeNineSliceRegion(uint8_t* dst, int32_t targetWidth, int32_t targetHeight, const Sprite* source,
                                 const SliceRegion& region)
{
    uint32_t bytesPerPixel = Deki::Texture2D::GetBytesPerPixel(source->format);

    // Source regions, inside `region`
    const int32_t rx = region.x, ry = region.y, rw = region.width, rh = region.height;
    int32_t srcLeft = region.left;
    int32_t srcRight = region.right;
    int32_t srcTop = region.top;
    int32_t srcBottom = region.bottom;
    int32_t srcCenterW = rw - srcLeft - srcRight;
    int32_t srcCenterH = rh - srcTop - srcBottom;

    // Destination regions
    int32_t dstLeft = srcLeft;
    int32_t dstRight = srcRight;
    int32_t dstTop = srcTop;
    int32_t dstBottom = srcBottom;
    int32_t dstCenterW = targetWidth - dstLeft - dstRight;
    int32_t dstCenterH = targetHeight - dstTop - dstBottom;

    // Copies a block of pixels with nearest-neighbour scaling. srcX/srcY are
    // inside the region; the region's offset is added here.
    auto copyRegion = [rx, ry](uint8_t* dst, int32_t dstWidth, int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                               const uint8_t* src, int32_t srcWidth, int32_t srcX, int32_t srcY, int32_t srcW,
                               int32_t srcH, uint32_t bytesPerPixel)
    {
        for (int32_t dy = 0; dy < dstH; dy++)
        {
            int32_t sy = (dy * srcH) / dstH;
            const uint8_t* srcRow = src + ((ry + srcY + sy) * srcWidth + rx + srcX) * bytesPerPixel;
            uint8_t* dstRow = dst + ((dstY + dy) * dstWidth + dstX) * bytesPerPixel;

            for (int32_t dx = 0; dx < dstW; dx++)
            {
                int32_t sx = (dx * srcW) / dstW;
                const uint8_t* srcPixel = srcRow + sx * bytesPerPixel;
                uint8_t* dstPixel = dstRow + dx * bytesPerPixel;

                for (uint32_t b = 0; b < bytesPerPixel; b++)
                {
                    dstPixel[b] = srcPixel[b];
                }
            }
        }
    };

    // The 9 regions:
    // +----+--------+----+
    // | TL |  Top   | TR |
    // +----+--------+----+
    // | L  | Center | R  |
    // +----+--------+----+
    // | BL | Bottom | BR |
    // +----+--------+----+

    // Top-left corner (copy as-is)
    if (srcLeft > 0 && srcTop > 0)
    {
        copyRegion(dst, targetWidth, 0, 0, dstLeft, dstTop, source->data, source->width, 0, 0, srcLeft, srcTop,
                   bytesPerPixel);
    }

    // Top edge (stretch horizontally)
    if (srcTop > 0 && srcCenterW > 0)
    {
        copyRegion(dst, targetWidth, dstLeft, 0, dstCenterW, dstTop, source->data, source->width, srcLeft, 0,
                   srcCenterW, srcTop, bytesPerPixel);
    }

    // Top-right corner (copy as-is)
    if (srcRight > 0 && srcTop > 0)
    {
        copyRegion(dst, targetWidth, targetWidth - dstRight, 0, dstRight, dstTop, source->data, source->width,
                   rw - srcRight, 0, srcRight, srcTop, bytesPerPixel);
    }

    // Left edge (stretch vertically)
    if (srcLeft > 0 && srcCenterH > 0)
    {
        copyRegion(dst, targetWidth, 0, dstTop, dstLeft, dstCenterH, source->data, source->width, 0, srcTop, srcLeft,
                   srcCenterH, bytesPerPixel);
    }

    // Center (stretch both directions)
    if (srcCenterW > 0 && srcCenterH > 0)
    {
        copyRegion(dst, targetWidth, dstLeft, dstTop, dstCenterW, dstCenterH, source->data, source->width, srcLeft,
                   srcTop, srcCenterW, srcCenterH, bytesPerPixel);
    }

    // Right edge (stretch vertically)
    if (srcRight > 0 && srcCenterH > 0)
    {
        copyRegion(dst, targetWidth, targetWidth - dstRight, dstTop, dstRight, dstCenterH, source->data, source->width,
                   rw - srcRight, srcTop, srcRight, srcCenterH, bytesPerPixel);
    }

    // Bottom-left corner (copy as-is)
    if (srcLeft > 0 && srcBottom > 0)
    {
        copyRegion(dst, targetWidth, 0, targetHeight - dstBottom, dstLeft, dstBottom, source->data, source->width, 0,
                   rh - srcBottom, srcLeft, srcBottom, bytesPerPixel);
    }

    // Bottom edge (stretch horizontally)
    if (srcCenterW > 0 && srcBottom > 0)
    {
        copyRegion(dst, targetWidth, dstLeft, targetHeight - dstBottom, dstCenterW, dstBottom, source->data,
                   source->width, srcLeft, rh - srcBottom, srcCenterW, srcBottom, bytesPerPixel);
    }

    // Bottom-right corner (copy as-is)
    if (srcRight > 0 && srcBottom > 0)
    {
        copyRegion(dst, targetWidth, targetWidth - dstRight, targetHeight - dstBottom, dstRight, dstBottom,
                   source->data, source->width, rw - srcRight, rh - srcBottom, srcRight, srcBottom, bytesPerPixel);
    }
}

Sprite* Sprite::CreateNineSlice(Sprite* source, int32_t targetWidth, int32_t targetHeight)
{
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

    int32_t minWidth = source->nineSliceLeft + source->nineSliceRight;
    int32_t minHeight = source->nineSliceTop + source->nineSliceBottom;

    if (targetWidth < minWidth || targetHeight < minHeight)
    {
        DEKI_LOG_ERROR("CreateNineSlice: Target size %dx%d too small (min: %dx%d)", targetWidth, targetHeight, minWidth,
                       minHeight);
        return nullptr;
    }

    Sprite* result = new Sprite();
    result->width = targetWidth;
    result->height = targetHeight;
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
    // chromaRowSpans is not copied; see CreateTiled.

    result->hasNineSlice = source->hasNineSlice;
    result->nineSliceLeft = source->nineSliceLeft;
    result->nineSliceRight = source->nineSliceRight;
    result->nineSliceTop = source->nineSliceTop;
    result->nineSliceBottom = source->nineSliceBottom;

    uint32_t bytesPerPixel = Deki::Texture2D::GetBytesPerPixel(source->format);
    size_t resultDataSize = targetWidth * targetHeight * bytesPerPixel;
    result->data = (uint8_t*)Deki::Memory::Allocate(resultDataSize, Deki::Memory::External);

    if (!result->data)
    {
        DEKI_LOG_ERROR("CreateNineSlice: Failed to allocate memory for scaled sprite");
        delete result;
        return nullptr;
    }

    BakeNineSliceInto(result->data, targetWidth, targetHeight, source);

    DEKI_LOG_INTERNAL("Created 9-slice sprite: %dx%d -> %dx%d", source->width, source->height, targetWidth,
                      targetHeight);

    return result;
}

// Registers the sprite loader with the AssetManager at static init.
namespace
{
struct SpriteLoaderReg
{
    SpriteLoaderReg()
    {
        Deki::AssetManager::RegisterLoader(
            "Sprite",
            [](const char* p) -> void*
            {
                auto* s = Sprite::Load(p);
                if (s)
                {
                    Deki::Time::Delay(1);  // yields, so the watchdog on a device does not fire
                }
                return s;
            },
            [](void* a) { delete static_cast<Sprite*>(a); },
            [](const uint8_t* d, size_t s) -> void* { return Sprite::LoadFromFileData(d, s); });
        // "Texture" loads the same way.
        Deki::AssetManager::RegisterLoader(
            "Texture",
            [](const char* p) -> void*
            {
                auto* s = Sprite::Load(p);
                if (s)
                {
                    Deki::Time::Delay(1);
                }
                return s;
            },
            [](void* a) { delete static_cast<Sprite*>(a); },
            [](const uint8_t* d, size_t s) -> void* { return Sprite::LoadFromFileData(d, s); });
    }
};
static SpriteLoaderReg s_SpriteLoaderReg;
}  // namespace

}  // namespace Deki2D
