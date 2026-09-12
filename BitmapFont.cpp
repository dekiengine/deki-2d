#include "BitmapFont.h"
#include <deki/providers/Memory.h>
#include "Sprite.h"
#include <deki/providers/FileSystem.h>
#include <deki/LogSystem.h>
#include <deki/Time.h>
#include <deki/assets/AssetManager.h>
#include <deki/assets/AssetPackReader.h>
#include <cstring>
 
BitmapFont::BitmapFont()
    : atlas(nullptr)
    , glyphs(nullptr)
    , codepoints(nullptr)
    , m_FirstChar(0)
    , m_LastChar(0)
    , m_LineHeight(0)
    , baseline(0)
    , m_CapHeight(0)
    , m_XHeight(0)
    , m_DecorationMode(0)
    , m_DecorationA(0)
    , m_DecorationB(0)
    , m_GlyphCount(0)
    , m_IsSparse(false)
{
}

BitmapFont::~BitmapFont()
{
    delete atlas;
    Deki::Memory::Free(glyphs);
    Deki::Memory::Free(codepoints);
}

BitmapFont* BitmapFont::Load(const char* file_path)
{
    if (!file_path)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: null file path");
        return nullptr;
    }

    uint32_t tStart = Deki::Time::GetTime();

    // Read entire file
    Deki::IFileSystem* fs = Deki::FileSystem::GetFileSystemForPath(file_path);
    if (!fs)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: No filesystem available for path: %s", file_path);
        return nullptr;
    }

    // Open file
    Deki::IFileSystem::FileHandle file = fs->OpenFile(file_path, Deki::IFileSystem::OpenMode::READ_BINARY);
    if (!file)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Failed to open file '%s'", file_path);
        return nullptr;
    }

    // Get file size
    long file_size = fs->GetFileSize(file);
    if (file_size <= 0)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Failed to get file size '%s'", file_path);
        fs->CloseFile(file);
        return nullptr;
    }

    // Allocate buffer and read entire file
    uint8_t* file_data =
        Deki::Memory::AllocateArray<uint8_t>(static_cast<size_t>(file_size), Deki::MemoryUse::Buffer,
                                             "BitmapFont::file");
    if (!file_data)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: no room for a %ld byte font file '%s'", file_size, file_path);
        fs->CloseFile(file);
        return nullptr;
    }
    size_t bytes_read = fs->ReadFile(file, file_data, file_size);
    fs->CloseFile(file);

    if (bytes_read != static_cast<size_t>(file_size))
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Failed to read file '%s'", file_path);
        Deki::Memory::Free(file_data);
        return nullptr;
    }

    // Validate minimum size
    if (static_cast<size_t>(file_size) < sizeof(FontHeader))
    {
        DEKI_LOG_ERROR("BitmapFont::Load: File too small for header");
        Deki::Memory::Free(file_data);
        return nullptr;
    }

    // Parse header
    FontHeader header;
    memcpy(&header, file_data, sizeof(FontHeader));

    // Validate magic
    if (memcmp(header.magic, "DFNT", 4) != 0)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Invalid magic (expected DFNT)");
        Deki::Memory::Free(file_data);
        return nullptr;
    }

    // Validate version (low 31 bits — v3/v4 encode sparse flag in high bit)
    const uint32_t versionLow = header.version & 0x7FFFFFFFu;
    if (versionLow != 1 && versionLow != 2 && versionLow != 3 && versionLow != 4)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Unsupported version %u", header.version);
        Deki::Memory::Free(file_data);
        return nullptr;
    }

    BitmapFont* font = new BitmapFont();
    const char* atlas_rel_path = nullptr;

    if (versionLow == 4)
    {
        FontHeaderV4 headerV4;
        if (static_cast<size_t>(file_size) < sizeof(FontHeaderV4))
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v4 header");
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }
        memcpy(&headerV4, file_data, sizeof(FontHeaderV4));
        const bool sparse = (headerV4.version & 0x80000000u) != 0;

        size_t codepoints_size = sparse ? headerV4.m_GlyphCount * sizeof(uint32_t) : 0;
        size_t glyphs_size = headerV4.m_GlyphCount * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV4) + codepoints_size + glyphs_size + headerV4.atlasPathLen;
        if (static_cast<size_t>(file_size) < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v4 glyph data");
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV4.firstCodepoint;
        font->m_LastChar = headerV4.lastCodepoint;
        font->m_LineHeight = headerV4.m_LineHeight;
        font->baseline = headerV4.baseline;
        font->m_CapHeight = headerV4.m_CapHeight;
        font->m_XHeight = headerV4.m_XHeight;
        font->m_DecorationMode = headerV4.m_DecorationMode;
        font->m_DecorationA = headerV4.m_DecorationA;
        font->m_DecorationB = headerV4.m_DecorationB;
        font->m_GlyphCount = headerV4.m_GlyphCount;
        font->m_IsSparse = sparse;

        size_t offset = sizeof(FontHeaderV4);
        if (sparse)
        {
            font->codepoints = Deki::Memory::AllocateArray<uint32_t>(headerV4.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::codepoints");
            if (!font->codepoints)
            {
                DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV4.m_GlyphCount);
                Deki::Memory::Free(file_data);
                delete font;
                return nullptr;
            }
            memcpy(font->codepoints, file_data + offset, codepoints_size);
            offset += codepoints_size;
        }

        font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(headerV4.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::glyphs");
        if (!font->glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV4.m_GlyphCount);
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }
        memcpy(font->glyphs, file_data + offset, glyphs_size);
        offset += glyphs_size;

        atlas_rel_path = (const char*)(file_data + offset);
    }
    else if (versionLow == 3)
    {
        FontHeaderV3 headerV3;
        if (static_cast<size_t>(file_size) < sizeof(FontHeaderV3))
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v3 header");
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }
        memcpy(&headerV3, file_data, sizeof(FontHeaderV3));
        const bool sparse = (headerV3.version & 0x80000000u) != 0;

        size_t codepoints_size = sparse ? headerV3.m_GlyphCount * sizeof(uint32_t) : 0;
        size_t glyphs_size = headerV3.m_GlyphCount * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV3) + codepoints_size + glyphs_size + headerV3.atlasPathLen;
        if (static_cast<size_t>(file_size) < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v3 glyph data");
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV3.firstCodepoint;
        font->m_LastChar = headerV3.lastCodepoint;
        font->m_LineHeight = headerV3.m_LineHeight;
        font->baseline = headerV3.baseline;
        font->m_CapHeight = headerV3.m_CapHeight;
        font->m_XHeight = headerV3.m_XHeight;
        font->m_GlyphCount = headerV3.m_GlyphCount;
        font->m_IsSparse = sparse;

        size_t offset = sizeof(FontHeaderV3);
        if (sparse)
        {
            font->codepoints = Deki::Memory::AllocateArray<uint32_t>(headerV3.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::codepoints");
            if (!font->codepoints)
            {
                DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV3.m_GlyphCount);
                Deki::Memory::Free(file_data);
                delete font;
                return nullptr;
            }
            memcpy(font->codepoints, file_data + offset, codepoints_size);
            offset += codepoints_size;
        }

        font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(headerV3.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::glyphs");
        if (!font->glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV3.m_GlyphCount);
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }
        memcpy(font->glyphs, file_data + offset, glyphs_size);
        offset += glyphs_size;

        atlas_rel_path = (const char*)(file_data + offset);
    }
    else if (versionLow == 1)
    {
        // V1: contiguous ASCII glyph array
        uint16_t expected_glyph_count = header.m_LastChar - header.m_FirstChar + 1;
        if (header.m_GlyphCount != expected_glyph_count)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: Glyph count mismatch (got %u, expected %u)",
                           header.m_GlyphCount, expected_glyph_count);
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }

        size_t glyphs_size = header.m_GlyphCount * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeader) + glyphs_size + header.atlasPathLen;
        if (static_cast<size_t>(file_size) < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v1 glyph data");
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }

        font->m_FirstChar = header.m_FirstChar;
        font->m_LastChar = header.m_LastChar;
        font->m_LineHeight = header.m_LineHeight;
        font->baseline = header.baseline;
        font->m_GlyphCount = header.m_GlyphCount;
        font->m_IsSparse = false;

        font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(header.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::glyphs");
        if (!font->glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)header.m_GlyphCount);
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }
        memcpy(font->glyphs, file_data + sizeof(FontHeader), glyphs_size);
        atlas_rel_path = (const char*)(file_data + sizeof(FontHeader) + glyphs_size);
    }
    else // versionLow == 2
    {
        // V2: sparse codepoint table + glyph array
        FontHeaderV2 headerV2;
        if (static_cast<size_t>(file_size) < sizeof(FontHeaderV2))
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v2 header");
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }
        memcpy(&headerV2, file_data, sizeof(FontHeaderV2));

        size_t codepoints_size = headerV2.m_GlyphCount * sizeof(uint32_t);
        size_t glyphs_size = headerV2.m_GlyphCount * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV2) + codepoints_size + glyphs_size + headerV2.atlasPathLen;
        if (static_cast<size_t>(file_size) < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v2 glyph data");
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV2.firstCodepoint;
        font->m_LastChar = headerV2.lastCodepoint;
        font->m_LineHeight = headerV2.m_LineHeight;
        font->baseline = headerV2.baseline;
        font->m_GlyphCount = headerV2.m_GlyphCount;
        font->m_IsSparse = true;

        font->codepoints = Deki::Memory::AllocateArray<uint32_t>(headerV2.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::codepoints");
        if (!font->codepoints)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV2.m_GlyphCount);
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }
        memcpy(font->codepoints, file_data + sizeof(FontHeaderV2), codepoints_size);

        font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(headerV2.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::glyphs");
        if (!font->glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV2.m_GlyphCount);
            Deki::Memory::Free(file_data);
            delete font;
            return nullptr;
        }
        memcpy(font->glyphs, file_data + sizeof(FontHeaderV2) + codepoints_size, glyphs_size);

        atlas_rel_path = (const char*)(file_data + sizeof(FontHeaderV2) + codepoints_size + glyphs_size);
    }

    // Build absolute path to atlas (same directory as font file)
    std::string font_path(file_path);
    size_t last_slash = font_path.find_last_of("/\\");
    std::string atlas_path;
    if (last_slash != std::string::npos)
    {
        atlas_path = font_path.substr(0, last_slash + 1) + atlas_rel_path;
    }
    else
    {
        atlas_path = atlas_rel_path;
    }

    Deki::Memory::Free(file_data);

    // Defer atlas loading to first GetAtlas() call for faster scene transitions
    font->m_AtlasPath = atlas_path;
    font->atlas = nullptr;

    uint32_t tEnd = Deki::Time::GetTime();
    DEKI_LOG_INTERNAL("[PERF] BitmapFont::Load: %ums '%s' (%u glyphs, atlas deferred)",
                      tEnd - tStart, file_path, font->m_GlyphCount);

    return font;
}

BitmapFont* BitmapFont::LoadFromFileData(const uint8_t* data, size_t size)
{
    if (!data || size < sizeof(FontHeader))
    {
        DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: invalid data");
        return nullptr;
    }

    FontHeader header;
    memcpy(&header, data, sizeof(FontHeader));

    const uint32_t versionLow = header.version & 0x7FFFFFFFu;
    if (memcmp(header.magic, "DFNT", 4) != 0 ||
        (versionLow != 1 && versionLow != 2 && versionLow != 3 && versionLow != 4))
    {
        DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: invalid magic/version");
        return nullptr;
    }

    BitmapFont* font = new BitmapFont();
    const char* atlas_rel_path = nullptr;

    if (versionLow == 4)
    {
        FontHeaderV4 headerV4;
        if (size < sizeof(FontHeaderV4))
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v4");
            delete font;
            return nullptr;
        }
        memcpy(&headerV4, data, sizeof(FontHeaderV4));
        const bool sparse = (headerV4.version & 0x80000000u) != 0;

        size_t codepoints_size = sparse ? headerV4.m_GlyphCount * sizeof(uint32_t) : 0;
        size_t glyphs_size = headerV4.m_GlyphCount * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV4) + codepoints_size + glyphs_size + headerV4.atlasPathLen;
        if (size < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v4 glyphs");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV4.firstCodepoint;
        font->m_LastChar = headerV4.lastCodepoint;
        font->m_LineHeight = headerV4.m_LineHeight;
        font->baseline = headerV4.baseline;
        font->m_CapHeight = headerV4.m_CapHeight;
        font->m_XHeight = headerV4.m_XHeight;
        font->m_DecorationMode = headerV4.m_DecorationMode;
        font->m_DecorationA = headerV4.m_DecorationA;
        font->m_DecorationB = headerV4.m_DecorationB;
        font->m_GlyphCount = headerV4.m_GlyphCount;
        font->m_IsSparse = sparse;

        size_t offset = sizeof(FontHeaderV4);
        if (sparse)
        {
            font->codepoints = Deki::Memory::AllocateArray<uint32_t>(headerV4.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::codepoints");
            if (!font->codepoints)
            {
                DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV4.m_GlyphCount);
                delete font;
                return nullptr;
            }
            memcpy(font->codepoints, data + offset, codepoints_size);
            offset += codepoints_size;
        }
        font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(headerV4.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::glyphs");
        if (!font->glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV4.m_GlyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->glyphs, data + offset, glyphs_size);
        offset += glyphs_size;
        atlas_rel_path = (const char*)(data + offset);
    }
    else if (versionLow == 3)
    {
        FontHeaderV3 headerV3;
        if (size < sizeof(FontHeaderV3))
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v3");
            delete font;
            return nullptr;
        }
        memcpy(&headerV3, data, sizeof(FontHeaderV3));
        const bool sparse = (headerV3.version & 0x80000000u) != 0;

        size_t codepoints_size = sparse ? headerV3.m_GlyphCount * sizeof(uint32_t) : 0;
        size_t glyphs_size = headerV3.m_GlyphCount * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV3) + codepoints_size + glyphs_size + headerV3.atlasPathLen;
        if (size < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v3 glyphs");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV3.firstCodepoint;
        font->m_LastChar = headerV3.lastCodepoint;
        font->m_LineHeight = headerV3.m_LineHeight;
        font->baseline = headerV3.baseline;
        font->m_CapHeight = headerV3.m_CapHeight;
        font->m_XHeight = headerV3.m_XHeight;
        font->m_GlyphCount = headerV3.m_GlyphCount;
        font->m_IsSparse = sparse;

        size_t offset = sizeof(FontHeaderV3);
        if (sparse)
        {
            font->codepoints = Deki::Memory::AllocateArray<uint32_t>(headerV3.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::codepoints");
            if (!font->codepoints)
            {
                DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV3.m_GlyphCount);
                delete font;
                return nullptr;
            }
            memcpy(font->codepoints, data + offset, codepoints_size);
            offset += codepoints_size;
        }
        font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(headerV3.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::glyphs");
        if (!font->glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV3.m_GlyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->glyphs, data + offset, glyphs_size);
        offset += glyphs_size;
        atlas_rel_path = (const char*)(data + offset);
    }
    else if (versionLow == 1)
    {
        uint16_t expected_glyph_count = header.m_LastChar - header.m_FirstChar + 1;
        if (header.m_GlyphCount != expected_glyph_count)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: glyph count mismatch");
            delete font;
            return nullptr;
        }

        size_t glyphs_size = header.m_GlyphCount * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeader) + glyphs_size + header.atlasPathLen;
        if (size < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = header.m_FirstChar;
        font->m_LastChar = header.m_LastChar;
        font->m_LineHeight = header.m_LineHeight;
        font->baseline = header.baseline;
        font->m_GlyphCount = header.m_GlyphCount;
        font->m_IsSparse = false;

        font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(header.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::glyphs");
        if (!font->glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)header.m_GlyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->glyphs, data + sizeof(FontHeader), glyphs_size);
        atlas_rel_path = (const char*)(data + sizeof(FontHeader) + glyphs_size);
    }
    else // versionLow == 2
    {
        FontHeaderV2 headerV2;
        if (size < sizeof(FontHeaderV2))
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v2");
            delete font;
            return nullptr;
        }
        memcpy(&headerV2, data, sizeof(FontHeaderV2));

        size_t codepoints_size = headerV2.m_GlyphCount * sizeof(uint32_t);
        size_t glyphs_size = headerV2.m_GlyphCount * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV2) + codepoints_size + glyphs_size + headerV2.atlasPathLen;
        if (size < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v2 glyphs");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV2.firstCodepoint;
        font->m_LastChar = headerV2.lastCodepoint;
        font->m_LineHeight = headerV2.m_LineHeight;
        font->baseline = headerV2.baseline;
        font->m_GlyphCount = headerV2.m_GlyphCount;
        font->m_IsSparse = true;

        font->codepoints = Deki::Memory::AllocateArray<uint32_t>(headerV2.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::codepoints");
        if (!font->codepoints)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV2.m_GlyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->codepoints, data + sizeof(FontHeaderV2), codepoints_size);

        font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(headerV2.m_GlyphCount, Deki::MemoryUse::Hot, "BitmapFont::glyphs");
        if (!font->glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV2.m_GlyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->glyphs, data + sizeof(FontHeaderV2) + codepoints_size, glyphs_size);

        atlas_rel_path = (const char*)(data + sizeof(FontHeaderV2) + codepoints_size + glyphs_size);
    }

    // Font loaded from pack — atlas path is relative, store as-is.
    font->m_AtlasPath = std::string(atlas_rel_path);
    font->atlas = nullptr;

    DEKI_LOG_INTERNAL("[PERF] BitmapFont::LoadFromFileData: %u glyphs (from pack, atlas deferred, v%s)",
                      font->m_GlyphCount, font->m_IsSparse ? "2" : "1");
    return font;
}

BitmapFont* BitmapFont::CreateMonospace(const char* atlas_path,
                                        uint8_t glyph_width,
                                        uint8_t glyph_height,
                                        uint8_t m_FirstChar,
                                        uint8_t chars_per_row,
                                        uint8_t char_count)
{
    if (!atlas_path || char_count == 0)
    {
        DEKI_LOG_ERROR("BitmapFont::CreateMonospace: Invalid parameters");
        return nullptr;
    }

    // Load atlas
    Sprite* atlas = Sprite::Load(atlas_path);
    if (!atlas)
    {
        DEKI_LOG_ERROR("BitmapFont::CreateMonospace: Failed to load atlas '%s'", atlas_path);
        return nullptr;
    }

    // Create font
    BitmapFont* font = new BitmapFont();
    font->atlas = atlas;
    font->m_FirstChar = m_FirstChar;
    font->m_LastChar = m_FirstChar + char_count - 1;
    font->m_LineHeight = glyph_height;
    font->baseline = glyph_height;  // Baseline at bottom for simple fonts
    font->m_GlyphCount = char_count;

    // Generate glyph data
    font->glyphs = Deki::Memory::AllocateArray<GlyphInfo>(char_count, Deki::MemoryUse::Hot,
                                                          "BitmapFont::generated");
    if (!font->glyphs)
    {
        DEKI_LOG_ERROR("BitmapFont: no room for %u generated glyphs", (unsigned)char_count);
        delete font;
        return nullptr;
    }
    for (uint8_t i = 0; i < char_count; i++)
    {
        uint8_t row = i / chars_per_row;
        uint8_t col = i % chars_per_row;

        font->glyphs[i].x = col * glyph_width;
        font->glyphs[i].y = row * glyph_height;
        font->glyphs[i].width = glyph_width;
        font->glyphs[i].height = glyph_height;
        font->glyphs[i].offsetX = 0;
        font->glyphs[i].offsetY = 0;
        font->glyphs[i].advance = glyph_width;
    }

    DEKI_LOG_INTERNAL("BitmapFont::CreateMonospace: Created font with %u glyphs (%dx%d)",
                  char_count, glyph_width, glyph_height);

    return font;
}

BitmapFont* BitmapFont::CreateFromMemory(Texture2D* atlas,
                                          GlyphInfo* glyphs,
                                          uint8_t m_FirstChar,
                                          uint8_t m_LastChar,
                                          uint8_t m_LineHeight,
                                          uint8_t baseline)
{
    if (!atlas || !glyphs || m_LastChar < m_FirstChar)
    {
        DEKI_LOG_ERROR("BitmapFont::CreateFromMemory: Invalid parameters");
        return nullptr;
    }

    BitmapFont* font = new BitmapFont();
    font->atlas = atlas;
    font->glyphs = glyphs;
    font->m_FirstChar = m_FirstChar;
    font->m_LastChar = m_LastChar;
    font->m_LineHeight = m_LineHeight;
    font->baseline = baseline;
    font->m_GlyphCount = m_LastChar - m_FirstChar + 1;

    DEKI_LOG_INTERNAL("BitmapFont::CreateFromMemory: Created font with %u glyphs, line height %u",
                  font->m_GlyphCount, m_LineHeight);

    return font;
}

Texture2D* BitmapFont::GetAtlas() const
{
    if (!atlas && !m_AtlasPath.empty())
    {
        uint32_t t0 = Deki::Time::GetTime();

        auto& packReader = Deki::AssetPackReader::Instance();
        if (packReader.HasPackIndex() && packReader.IsInPack(m_AtlasPath))
        {
            const auto* packAsset = packReader.GetAsset(m_AtlasPath);
            if (packAsset && packAsset->ptr && packAsset->size > 0)
            {
                atlas = Sprite::LoadFromFileData(packAsset->ptr, packAsset->size);
            }
        }
        else
        {
            atlas = Sprite::Load(m_AtlasPath.c_str());
        }

        uint32_t t1 = Deki::Time::GetTime();
        if (!atlas)
        {
            DEKI_LOG_ERROR("BitmapFont::GetAtlas: Failed to load atlas '%s'", m_AtlasPath.c_str());
        }
        else
        {
            DEKI_LOG_INTERNAL("[PERF] BitmapFont::GetAtlas: %ums (lazy load) %s", t1 - t0, m_AtlasPath.c_str());
        }
    }
    return atlas;
}

const GlyphInfo* BitmapFont::GetGlyph(char c) const
{
    return GetGlyphByCodepoint(static_cast<uint8_t>(c));
}

const GlyphInfo* BitmapFont::GetGlyphByCodepoint(uint32_t codepoint) const
{
    if (!glyphs || m_GlyphCount == 0)
        return nullptr;

    if (m_IsSparse && codepoints)
    {
        // Binary search in sorted codepoint table
        int lo = 0, hi = static_cast<int>(m_GlyphCount) - 1;
        while (lo <= hi)
        {
            int mid = lo + (hi - lo) / 2;
            if (codepoints[mid] == codepoint)
                return &glyphs[mid];
            else if (codepoints[mid] < codepoint)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        return nullptr;
    }
    else
    {
        // V1 contiguous: direct index
        if (codepoint < m_FirstChar || codepoint > m_LastChar)
            return nullptr;
        return &glyphs[codepoint - m_FirstChar];
    }
}

int32_t BitmapFont::MeasureWidth(const char* text) const
{
    if (!text)
        return 0;
    return MeasureWidth(text, strlen(text));
}

int32_t BitmapFont::MeasureWidth(const char* text, size_t length) const
{
    if (!text || length == 0)
        return 0;

    int32_t width = 0;
    if (m_IsSparse)
    {
        // UTF-8 decode for sparse (v2) fonts
        size_t i = 0;
        while (i < length)
        {
            const uint32_t cp = DecodeUtf8(text, length, i);
            if (cp == 0xFFFD) continue;  // invalid byte: skipped, as before

            const GlyphInfo* glyph = GetGlyphByCodepoint(cp);
            if (glyph)
                width += glyph->advance;
        }
    }
    else
    {
        // V1 ASCII: direct byte lookup
        for (size_t i = 0; i < length; i++)
        {
            const GlyphInfo* glyph = GetGlyph(text[i]);
            if (glyph)
                width += glyph->advance;
        }
    }
    return width;
}

void BitmapFont::GetVisualBounds(int32_t& min_y, int32_t& max_y) const
{
    if (!glyphs || m_GlyphCount == 0)
    {
        min_y = 0;
        max_y = m_LineHeight;
        return;
    }
    if (m_VisualBoundsValid)
    {
        min_y = m_VisualMinY;
        max_y = m_VisualMaxY;
        return;
    }

    // Find the actual bounds by examining all glyphs
    min_y = INT32_MAX;
    max_y = INT32_MIN;

    for (uint16_t i = 0; i < m_GlyphCount; i++)
    {
        const GlyphInfo& g = glyphs[i];
        if (g.height == 0)
            continue;

        // offsetY is relative to baseline, positive means below baseline
        int32_t glyph_top = g.offsetY;
        int32_t glyph_bottom = g.offsetY + g.height;

        if (glyph_top < min_y)
            min_y = glyph_top;
        if (glyph_bottom > max_y)
            max_y = glyph_bottom;
    }

    // If no valid glyphs found, use defaults
    if (min_y == INT32_MAX)
    {
        min_y = 0;
        max_y = m_LineHeight;
    }
    m_VisualMinY = min_y;
    m_VisualMaxY = max_y;
    m_VisualBoundsValid = true;
}

int32_t BitmapFont::GetVisualCenterY() const
{
    int32_t min_y, max_y;
    GetVisualBounds(min_y, max_y);

    // Visual center is midpoint between top and bottom of glyph bounds
    // Return as offset from top of line (where textY starts)
    return (min_y + max_y) / 2;
}

uint8_t BitmapFont::GetCapHeight() const
{
    // v1/v2 fonts set m_CapHeight to 0; fall back to typographic approximation.
    if (m_CapHeight != 0)
        return m_CapHeight;
    return static_cast<uint8_t>((baseline * 7) / 10);
}

uint8_t BitmapFont::GetXHeight() const
{
    if (m_XHeight != 0)
        return m_XHeight;
    return static_cast<uint8_t>((baseline * 5) / 10);
}

int32_t BitmapFont::GetCapCenterY() const
{
    // Baseline measured from top-of-line; cap-height extends upward from baseline.
    // Optical cap center sits half a cap-height above the baseline.
    return static_cast<int32_t>(baseline) - static_cast<int32_t>(GetCapHeight()) / 2;
}

int32_t BitmapFont::GetXCenterY() const
{
    return static_cast<int32_t>(baseline) - static_cast<int32_t>(GetXHeight()) / 2;
}

// Self-register font loader with AssetManager
namespace {
    struct _FontLoaderReg {
        _FontLoaderReg() {
            Deki::AssetManager::RegisterLoader("BitmapFont",
                [](const char* p) -> void* {
                    auto* f = BitmapFont::Load(p);
                    if (f) Deki::Time::Delay(1); // Yield for watchdog on embedded
                    return f;
                },
                [](void* a) { delete static_cast<BitmapFont*>(a); },
                [](const uint8_t* d, size_t s) -> void* { return BitmapFont::LoadFromFileData(d, s); });
            // Also register as "Font" (alias)
            Deki::AssetManager::RegisterLoader("Font",
                [](const char* p) -> void* {
                    auto* f = BitmapFont::Load(p);
                    if (f) Deki::Time::Delay(1);
                    return f;
                },
                [](void* a) { delete static_cast<BitmapFont*>(a); },
                [](const uint8_t* d, size_t s) -> void* { return BitmapFont::LoadFromFileData(d, s); });
        }
    };
    static _FontLoaderReg s_fontLoaderReg;
}

uint32_t BitmapFont::DecodeUtf8(const char* str, size_t len, size_t& i)
{
    const uint8_t b0 = static_cast<uint8_t>(str[i]);
    if (b0 < 0x80)
    {
        i += 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && i + 1 < len)
    {
        uint32_t cp = (b0 & 0x1F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && i + 2 < len)
    {
        uint32_t cp = (b0 & 0x0F) << 12;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && i + 3 < len)
    {
        uint32_t cp = (b0 & 0x07) << 18;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F) << 12;
        cp |= (static_cast<uint8_t>(str[i + 2]) & 0x3F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    i += 1;  // skip the invalid byte
    return 0xFFFD;
}
