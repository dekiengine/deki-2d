#include "BitmapFont.h"
#include <deki/providers/Memory.h>
#include "Sprite.h"
#include <deki/providers/FileSystem.h>
#include <deki/LogSystem.h>
#include <deki/Time.h>
#include <deki/assets/AssetManager.h>
#include <deki/assets/AssetPackReader.h>
#include <cstring>
#include <utility>

namespace Deki2D
{

BitmapFont::BitmapFont()
    : m_Atlas(nullptr),
      m_FirstChar(0),
      m_LastChar(0),
      m_LineHeight(0),
      m_Baseline(0),
      m_CapHeight(0),
      m_XHeight(0),
      m_DecorationMode(0),
      m_DecorationA(0),
      m_DecorationB(0),
      m_GlyphCount(0),
      m_IsSparse(false)
{
}

BitmapFont::~BitmapFont()
{
    delete m_Atlas;
}

BitmapFont* BitmapFont::Load(const char* filePath)
{
    if (!filePath)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: null file path");
        return nullptr;
    }

    uint32_t tStart = Deki::Time::GetTime();

    Deki::IFileSystem* fs = Deki::FileSystem::GetFileSystemForPath(filePath);
    if (!fs)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: No filesystem available for path: %s", filePath);
        return nullptr;
    }

    Deki::IFileSystem::FileHandle file = fs->OpenFile(filePath, Deki::IFileSystem::OpenMode::ReadBinary);
    if (!file)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Failed to open file '%s'", filePath);
        return nullptr;
    }

    long fileSize = fs->GetFileSize(file);
    if (fileSize <= 0)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Failed to get file size '%s'", filePath);
        fs->CloseFile(file);
        return nullptr;
    }

    // Owned, so the many early exits below need no cleanup.
    Deki::Buffer<uint8_t> fileBuffer(static_cast<size_t>(fileSize), Deki::Memory::External);
    uint8_t* fileData = fileBuffer.Data();
    if (!fileData)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: no room for a %ld byte font file '%s'", fileSize, filePath);
        fs->CloseFile(file);
        return nullptr;
    }
    size_t bytesRead = fs->ReadFile(file, fileData, fileSize);
    fs->CloseFile(file);

    if (bytesRead != static_cast<size_t>(fileSize))
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Failed to read file '%s'", filePath);
        return nullptr;
    }

    if (static_cast<size_t>(fileSize) < sizeof(FontHeader))
    {
        DEKI_LOG_ERROR("BitmapFont::Load: File too small for header");
        return nullptr;
    }

    FontHeader header;
    memcpy(&header, fileData, sizeof(FontHeader));

    if (memcmp(header.magic, "DFNT", 4) != 0)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Invalid magic (expected DFNT)");
        return nullptr;
    }

    // Low 31 bits only: v3/v4 keep the sparse flag in the high bit
    const uint32_t versionLow = header.version & 0x7FFFFFFFu;
    if (versionLow != 1 && versionLow != 2 && versionLow != 3 && versionLow != 4)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Unsupported version %u", header.version);
        return nullptr;
    }

    BitmapFont* font = new BitmapFont();
    const char* atlasRelPath = nullptr;

    if (versionLow == 4)
    {
        FontHeaderV4 headerV4;
        if (static_cast<size_t>(fileSize) < sizeof(FontHeaderV4))
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v4 header");
            delete font;
            return nullptr;
        }
        memcpy(&headerV4, fileData, sizeof(FontHeaderV4));
        const bool sparse = (headerV4.version & 0x80000000u) != 0;

        size_t codepointsSize = sparse ? headerV4.glyphCount * sizeof(uint32_t) : 0;
        size_t glyphsSize = headerV4.glyphCount * sizeof(GlyphInfo);
        size_t minSize = sizeof(FontHeaderV4) + codepointsSize + glyphsSize + headerV4.atlasPathLen;
        if (static_cast<size_t>(fileSize) < minSize)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v4 glyph data");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV4.firstCodepoint;
        font->m_LastChar = headerV4.lastCodepoint;
        font->m_LineHeight = headerV4.lineHeight;
        font->m_Baseline = headerV4.baseline;
        font->m_CapHeight = headerV4.capHeight;
        font->m_XHeight = headerV4.xHeight;
        font->m_DecorationMode = headerV4.decorationMode;
        font->m_DecorationA = headerV4.decorationA;
        font->m_DecorationB = headerV4.decorationB;
        font->m_GlyphCount = headerV4.glyphCount;
        font->m_IsSparse = sparse;

        size_t offset = sizeof(FontHeaderV4);
        if (sparse)
        {
            font->m_Codepoints.Allocate(headerV4.glyphCount, Deki::Memory::Internal);
            if (!font->m_Codepoints)
            {
                DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV4.glyphCount);
                delete font;
                return nullptr;
            }
            memcpy(font->m_Codepoints.Data(), fileData + offset, codepointsSize);
            offset += codepointsSize;
        }

        font->m_Glyphs.Allocate(headerV4.glyphCount, Deki::Memory::Internal);
        if (!font->m_Glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV4.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Glyphs.Data(), fileData + offset, glyphsSize);
        offset += glyphsSize;

        atlasRelPath = (const char*)(fileData + offset);
    }
    else if (versionLow == 3)
    {
        FontHeaderV3 headerV3;
        if (static_cast<size_t>(fileSize) < sizeof(FontHeaderV3))
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v3 header");
            delete font;
            return nullptr;
        }
        memcpy(&headerV3, fileData, sizeof(FontHeaderV3));
        const bool sparse = (headerV3.version & 0x80000000u) != 0;

        size_t codepointsSize = sparse ? headerV3.glyphCount * sizeof(uint32_t) : 0;
        size_t glyphsSize = headerV3.glyphCount * sizeof(GlyphInfo);
        size_t minSize = sizeof(FontHeaderV3) + codepointsSize + glyphsSize + headerV3.atlasPathLen;
        if (static_cast<size_t>(fileSize) < minSize)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v3 glyph data");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV3.firstCodepoint;
        font->m_LastChar = headerV3.lastCodepoint;
        font->m_LineHeight = headerV3.lineHeight;
        font->m_Baseline = headerV3.baseline;
        font->m_CapHeight = headerV3.capHeight;
        font->m_XHeight = headerV3.xHeight;
        font->m_GlyphCount = headerV3.glyphCount;
        font->m_IsSparse = sparse;

        size_t offset = sizeof(FontHeaderV3);
        if (sparse)
        {
            font->m_Codepoints.Allocate(headerV3.glyphCount, Deki::Memory::Internal);
            if (!font->m_Codepoints)
            {
                DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV3.glyphCount);
                delete font;
                return nullptr;
            }
            memcpy(font->m_Codepoints.Data(), fileData + offset, codepointsSize);
            offset += codepointsSize;
        }

        font->m_Glyphs.Allocate(headerV3.glyphCount, Deki::Memory::Internal);
        if (!font->m_Glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV3.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Glyphs.Data(), fileData + offset, glyphsSize);
        offset += glyphsSize;

        atlasRelPath = (const char*)(fileData + offset);
    }
    else if (versionLow == 1)
    {
        // v1: contiguous ASCII glyph array
        uint16_t expectedGlyphCount = header.lastChar - header.firstChar + 1;
        if (header.glyphCount != expectedGlyphCount)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: Glyph count mismatch (got %u, expected %u)", header.glyphCount,
                           expectedGlyphCount);
            delete font;
            return nullptr;
        }

        size_t glyphsSize = header.glyphCount * sizeof(GlyphInfo);
        size_t minSize = sizeof(FontHeader) + glyphsSize + header.atlasPathLen;
        if (static_cast<size_t>(fileSize) < minSize)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v1 glyph data");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = header.firstChar;
        font->m_LastChar = header.lastChar;
        font->m_LineHeight = header.lineHeight;
        font->m_Baseline = header.baseline;
        font->m_GlyphCount = header.glyphCount;
        font->m_IsSparse = false;

        font->m_Glyphs.Allocate(header.glyphCount, Deki::Memory::Internal);
        if (!font->m_Glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)header.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Glyphs.Data(), fileData + sizeof(FontHeader), glyphsSize);
        atlasRelPath = (const char*)(fileData + sizeof(FontHeader) + glyphsSize);
    }
    else  // versionLow == 2
    {
        // v2: sparse codepoint table + glyph array
        FontHeaderV2 headerV2;
        if (static_cast<size_t>(fileSize) < sizeof(FontHeaderV2))
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v2 header");
            delete font;
            return nullptr;
        }
        memcpy(&headerV2, fileData, sizeof(FontHeaderV2));

        size_t codepointsSize = headerV2.glyphCount * sizeof(uint32_t);
        size_t glyphsSize = headerV2.glyphCount * sizeof(GlyphInfo);
        size_t minSize = sizeof(FontHeaderV2) + codepointsSize + glyphsSize + headerV2.atlasPathLen;
        if (static_cast<size_t>(fileSize) < minSize)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v2 glyph data");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV2.firstCodepoint;
        font->m_LastChar = headerV2.lastCodepoint;
        font->m_LineHeight = headerV2.lineHeight;
        font->m_Baseline = headerV2.baseline;
        font->m_GlyphCount = headerV2.glyphCount;
        font->m_IsSparse = true;

        font->m_Codepoints.Allocate(headerV2.glyphCount, Deki::Memory::Internal);
        if (!font->m_Codepoints)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV2.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Codepoints.Data(), fileData + sizeof(FontHeaderV2), codepointsSize);

        font->m_Glyphs.Allocate(headerV2.glyphCount, Deki::Memory::Internal);
        if (!font->m_Glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV2.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Glyphs.Data(), fileData + sizeof(FontHeaderV2) + codepointsSize, glyphsSize);

        atlasRelPath = (const char*)(fileData + sizeof(FontHeaderV2) + codepointsSize + glyphsSize);
    }

    // The atlas path is relative to the font file's folder
    std::string fontPath(filePath);
    size_t lastSlash = fontPath.find_last_of("/\\");
    std::string atlasPath;
    if (lastSlash != std::string::npos)
    {
        atlasPath = fontPath.substr(0, lastSlash + 1) + atlasRelPath;
    }
    else
    {
        atlasPath = atlasRelPath;
    }

    // The atlas loads on the first GetAtlas() call, for faster scene transitions
    font->m_AtlasPath = atlasPath;
    font->m_Atlas = nullptr;

    uint32_t tEnd = Deki::Time::GetTime();
    DEKI_LOG_INTERNAL("[PERF] BitmapFont::Load: %ums '%s' (%u glyphs, atlas deferred)", tEnd - tStart, filePath,
                      font->m_GlyphCount);

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
    const char* atlasRelPath = nullptr;

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

        size_t codepointsSize = sparse ? headerV4.glyphCount * sizeof(uint32_t) : 0;
        size_t glyphsSize = headerV4.glyphCount * sizeof(GlyphInfo);
        size_t minSize = sizeof(FontHeaderV4) + codepointsSize + glyphsSize + headerV4.atlasPathLen;
        if (size < minSize)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v4 glyphs");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV4.firstCodepoint;
        font->m_LastChar = headerV4.lastCodepoint;
        font->m_LineHeight = headerV4.lineHeight;
        font->m_Baseline = headerV4.baseline;
        font->m_CapHeight = headerV4.capHeight;
        font->m_XHeight = headerV4.xHeight;
        font->m_DecorationMode = headerV4.decorationMode;
        font->m_DecorationA = headerV4.decorationA;
        font->m_DecorationB = headerV4.decorationB;
        font->m_GlyphCount = headerV4.glyphCount;
        font->m_IsSparse = sparse;

        size_t offset = sizeof(FontHeaderV4);
        if (sparse)
        {
            font->m_Codepoints.Allocate(headerV4.glyphCount, Deki::Memory::Internal);
            if (!font->m_Codepoints)
            {
                DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV4.glyphCount);
                delete font;
                return nullptr;
            }
            memcpy(font->m_Codepoints.Data(), data + offset, codepointsSize);
            offset += codepointsSize;
        }
        font->m_Glyphs.Allocate(headerV4.glyphCount, Deki::Memory::Internal);
        if (!font->m_Glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV4.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Glyphs.Data(), data + offset, glyphsSize);
        offset += glyphsSize;
        atlasRelPath = (const char*)(data + offset);
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

        size_t codepointsSize = sparse ? headerV3.glyphCount * sizeof(uint32_t) : 0;
        size_t glyphsSize = headerV3.glyphCount * sizeof(GlyphInfo);
        size_t minSize = sizeof(FontHeaderV3) + codepointsSize + glyphsSize + headerV3.atlasPathLen;
        if (size < minSize)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v3 glyphs");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV3.firstCodepoint;
        font->m_LastChar = headerV3.lastCodepoint;
        font->m_LineHeight = headerV3.lineHeight;
        font->m_Baseline = headerV3.baseline;
        font->m_CapHeight = headerV3.capHeight;
        font->m_XHeight = headerV3.xHeight;
        font->m_GlyphCount = headerV3.glyphCount;
        font->m_IsSparse = sparse;

        size_t offset = sizeof(FontHeaderV3);
        if (sparse)
        {
            font->m_Codepoints.Allocate(headerV3.glyphCount, Deki::Memory::Internal);
            if (!font->m_Codepoints)
            {
                DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV3.glyphCount);
                delete font;
                return nullptr;
            }
            memcpy(font->m_Codepoints.Data(), data + offset, codepointsSize);
            offset += codepointsSize;
        }
        font->m_Glyphs.Allocate(headerV3.glyphCount, Deki::Memory::Internal);
        if (!font->m_Glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV3.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Glyphs.Data(), data + offset, glyphsSize);
        offset += glyphsSize;
        atlasRelPath = (const char*)(data + offset);
    }
    else if (versionLow == 1)
    {
        uint16_t expectedGlyphCount = header.lastChar - header.firstChar + 1;
        if (header.glyphCount != expectedGlyphCount)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: glyph count mismatch");
            delete font;
            return nullptr;
        }

        size_t glyphsSize = header.glyphCount * sizeof(GlyphInfo);
        size_t minSize = sizeof(FontHeader) + glyphsSize + header.atlasPathLen;
        if (size < minSize)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = header.firstChar;
        font->m_LastChar = header.lastChar;
        font->m_LineHeight = header.lineHeight;
        font->m_Baseline = header.baseline;
        font->m_GlyphCount = header.glyphCount;
        font->m_IsSparse = false;

        font->m_Glyphs.Allocate(header.glyphCount, Deki::Memory::Internal);
        if (!font->m_Glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)header.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Glyphs.Data(), data + sizeof(FontHeader), glyphsSize);
        atlasRelPath = (const char*)(data + sizeof(FontHeader) + glyphsSize);
    }
    else  // versionLow == 2
    {
        FontHeaderV2 headerV2;
        if (size < sizeof(FontHeaderV2))
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v2");
            delete font;
            return nullptr;
        }
        memcpy(&headerV2, data, sizeof(FontHeaderV2));

        size_t codepointsSize = headerV2.glyphCount * sizeof(uint32_t);
        size_t glyphsSize = headerV2.glyphCount * sizeof(GlyphInfo);
        size_t minSize = sizeof(FontHeaderV2) + codepointsSize + glyphsSize + headerV2.atlasPathLen;
        if (size < minSize)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v2 glyphs");
            delete font;
            return nullptr;
        }

        font->m_FirstChar = headerV2.firstCodepoint;
        font->m_LastChar = headerV2.lastCodepoint;
        font->m_LineHeight = headerV2.lineHeight;
        font->m_Baseline = headerV2.baseline;
        font->m_GlyphCount = headerV2.glyphCount;
        font->m_IsSparse = true;

        font->m_Codepoints.Allocate(headerV2.glyphCount, Deki::Memory::Internal);
        if (!font->m_Codepoints)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u codepoints", (unsigned)headerV2.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Codepoints.Data(), data + sizeof(FontHeaderV2), codepointsSize);

        font->m_Glyphs.Allocate(headerV2.glyphCount, Deki::Memory::Internal);
        if (!font->m_Glyphs)
        {
            DEKI_LOG_ERROR("BitmapFont: no room for %u glyphs", (unsigned)headerV2.glyphCount);
            delete font;
            return nullptr;
        }
        memcpy(font->m_Glyphs.Data(), data + sizeof(FontHeaderV2) + codepointsSize, glyphsSize);

        atlasRelPath = (const char*)(data + sizeof(FontHeaderV2) + codepointsSize + glyphsSize);
    }

    // From a pack: the atlas path is a relative asset path, kept as it is
    font->m_AtlasPath = std::string(atlasRelPath);
    font->m_Atlas = nullptr;

    DEKI_LOG_INTERNAL("[PERF] BitmapFont::LoadFromFileData: %u glyphs (from pack, atlas deferred, v%s)",
                      font->m_GlyphCount, font->m_IsSparse ? "2" : "1");
    return font;
}

BitmapFont* BitmapFont::CreateMonospace(const char* atlasPath, uint8_t glyphWidth, uint8_t glyphHeight,
                                        uint8_t firstChar, uint8_t charsPerRow, uint8_t charCount)
{
    if (!atlasPath || charCount == 0)
    {
        DEKI_LOG_ERROR("BitmapFont::CreateMonospace: Invalid parameters");
        return nullptr;
    }

    Sprite* atlas = Sprite::Load(atlasPath);
    if (!atlas)
    {
        DEKI_LOG_ERROR("BitmapFont::CreateMonospace: Failed to load atlas '%s'", atlasPath);
        return nullptr;
    }

    BitmapFont* font = new BitmapFont();
    font->m_Atlas = atlas;
    font->m_FirstChar = firstChar;
    font->m_LastChar = firstChar + charCount - 1;
    font->m_LineHeight = glyphHeight;
    font->m_Baseline = glyphHeight;  // Baseline at bottom for simple fonts
    font->m_GlyphCount = charCount;

    font->m_Glyphs.Allocate(charCount, Deki::Memory::Internal);
    if (!font->m_Glyphs)
    {
        DEKI_LOG_ERROR("BitmapFont: no room for %u generated glyphs", (unsigned)charCount);
        delete font;
        return nullptr;
    }
    for (uint8_t i = 0; i < charCount; i++)
    {
        uint8_t row = i / charsPerRow;
        uint8_t col = i % charsPerRow;

        font->m_Glyphs[i].x = col * glyphWidth;
        font->m_Glyphs[i].y = row * glyphHeight;
        font->m_Glyphs[i].width = glyphWidth;
        font->m_Glyphs[i].height = glyphHeight;
        font->m_Glyphs[i].offsetX = 0;
        font->m_Glyphs[i].offsetY = 0;
        font->m_Glyphs[i].advance = glyphWidth;
    }

    DEKI_LOG_INTERNAL("BitmapFont::CreateMonospace: Created font with %u glyphs (%dx%d)", charCount, glyphWidth,
                      glyphHeight);

    return font;
}

BitmapFont* BitmapFont::CreateFromMemory(Deki::Texture2D* atlas, Deki::Buffer<GlyphInfo>&& glyphs, uint8_t firstChar,
                                         uint8_t lastChar, uint8_t lineHeight, uint8_t baseline)
{
    if (!atlas || !glyphs || lastChar < firstChar)
    {
        DEKI_LOG_ERROR("BitmapFont::CreateFromMemory: Invalid parameters");
        return nullptr;
    }

    BitmapFont* font = new BitmapFont();
    font->m_Atlas = atlas;
    font->m_Glyphs = std::move(glyphs);
    font->m_FirstChar = firstChar;
    font->m_LastChar = lastChar;
    font->m_LineHeight = lineHeight;
    font->m_Baseline = baseline;
    font->m_GlyphCount = lastChar - firstChar + 1;

    DEKI_LOG_INTERNAL("BitmapFont::CreateFromMemory: Created font with %u glyphs, line height %u", font->m_GlyphCount,
                      m_LineHeight);

    return font;
}

Deki::Texture2D* BitmapFont::GetAtlas() const
{
    if (!m_Atlas && !m_AtlasPath.empty())
    {
        uint32_t t0 = Deki::Time::GetTime();

        auto& packReader = Deki::AssetPackReader::Instance();
        if (packReader.HasPackIndex() && packReader.IsInPack(m_AtlasPath))
        {
            const auto* packAsset = packReader.GetAsset(m_AtlasPath);
            if (packAsset && packAsset->ptr && packAsset->size > 0)
            {
                m_Atlas = Sprite::LoadFromFileData(packAsset->ptr, packAsset->size);
            }
        }
        else
        {
            m_Atlas = Sprite::Load(m_AtlasPath.c_str());
        }

        uint32_t t1 = Deki::Time::GetTime();
        if (!m_Atlas)
        {
            DEKI_LOG_ERROR("BitmapFont::GetAtlas: Failed to load atlas '%s'", m_AtlasPath.c_str());
        }
        else
        {
            DEKI_LOG_INTERNAL("[PERF] BitmapFont::GetAtlas: %ums (lazy load) %s", t1 - t0, m_AtlasPath.c_str());
        }
    }
    return m_Atlas;
}

const GlyphInfo* BitmapFont::GetGlyph(char c) const
{
    return GetGlyphByCodepoint(static_cast<uint8_t>(c));
}

const GlyphInfo* BitmapFont::GetGlyphByCodepoint(uint32_t codepoint) const
{
    if (!m_Glyphs || m_GlyphCount == 0)
    {
        return nullptr;
    }

    if (m_IsSparse && m_Codepoints)
    {
        // Binary search in the sorted codepoint table
        int lo = 0, hi = static_cast<int>(m_GlyphCount) - 1;
        while (lo <= hi)
        {
            int mid = lo + (hi - lo) / 2;
            if (m_Codepoints[mid] == codepoint)
            {
                return &m_Glyphs[mid];
            }
            else if (m_Codepoints[mid] < codepoint)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }
        return nullptr;
    }
    else
    {
        // Contiguous range: direct index
        if (codepoint < m_FirstChar || codepoint > m_LastChar)
        {
            return nullptr;
        }
        return &m_Glyphs[codepoint - m_FirstChar];
    }
}

int32_t BitmapFont::MeasureWidth(const char* text) const
{
    if (!text)
    {
        return 0;
    }
    return MeasureWidth(text, strlen(text));
}

int32_t BitmapFont::MeasureWidth(const char* text, size_t length) const
{
    if (!text || length == 0)
    {
        return 0;
    }

    int32_t width = 0;
    if (m_IsSparse)
    {
        // Sparse fonts are Unicode, so decode UTF-8
        size_t i = 0;
        while (i < length)
        {
            const uint32_t cp = DecodeUtf8(text, length, i);
            if (cp == 0xFFFD)
            {
                continue;  // invalid byte: skipped
            }

            const GlyphInfo* glyph = GetGlyphByCodepoint(cp);
            if (glyph)
            {
                width += glyph->advance;
            }
        }
    }
    else
    {
        // Contiguous ASCII: one byte per character
        for (size_t i = 0; i < length; i++)
        {
            const GlyphInfo* glyph = GetGlyph(text[i]);
            if (glyph)
            {
                width += glyph->advance;
            }
        }
    }
    return width;
}

void BitmapFont::GetVisualBounds(int32_t& minY, int32_t& maxY) const
{
    if (!m_Glyphs || m_GlyphCount == 0)
    {
        minY = 0;
        maxY = m_LineHeight;
        return;
    }
    if (m_VisualBoundsValid)
    {
        minY = m_VisualMinY;
        maxY = m_VisualMaxY;
        return;
    }

    minY = INT32_MAX;
    maxY = INT32_MIN;

    for (uint16_t i = 0; i < m_GlyphCount; i++)
    {
        const GlyphInfo& g = m_Glyphs[i];
        if (g.height == 0)
        {
            continue;
        }

        // offsetY is relative to the baseline; positive is below it
        int32_t glyphTop = g.offsetY;
        int32_t glyphBottom = g.offsetY + g.height;

        if (glyphTop < minY)
        {
            minY = glyphTop;
        }
        if (glyphBottom > maxY)
        {
            maxY = glyphBottom;
        }
    }

    // No glyph has pixels: use the whole line
    if (minY == INT32_MAX)
    {
        minY = 0;
        maxY = m_LineHeight;
    }
    m_VisualMinY = minY;
    m_VisualMaxY = maxY;
    m_VisualBoundsValid = true;
}

int32_t BitmapFont::GetVisualCenterY() const
{
    int32_t minY, maxY;
    GetVisualBounds(minY, maxY);

    // Midpoint of the glyph bounds, as an offset from the top of the line
    // (where textY starts)
    return (minY + maxY) / 2;
}

uint8_t BitmapFont::GetCapHeight() const
{
    // v1/v2 fonts leave m_CapHeight at 0; use the usual typographic ratio.
    if (m_CapHeight != 0)
    {
        return m_CapHeight;
    }
    return static_cast<uint8_t>((m_Baseline * 7) / 10);
}

uint8_t BitmapFont::GetXHeight() const
{
    if (m_XHeight != 0)
    {
        return m_XHeight;
    }
    return static_cast<uint8_t>((m_Baseline * 5) / 10);
}

int32_t BitmapFont::GetCapCenterY() const
{
    // The baseline is measured from the top of the line and cap-height goes up
    // from it, so the cap centre is half a cap-height above the baseline.
    return static_cast<int32_t>(m_Baseline) - static_cast<int32_t>(GetCapHeight()) / 2;
}

int32_t BitmapFont::GetXCenterY() const
{
    return static_cast<int32_t>(m_Baseline) - static_cast<int32_t>(GetXHeight()) / 2;
}

// Registers the font loader with the AssetManager at startup
namespace
{
struct FontLoaderReg
{
    FontLoaderReg()
    {
        Deki::AssetManager::RegisterLoader(
            "BitmapFont",
            [](const char* p) -> void*
            {
                auto* f = BitmapFont::Load(p);
                if (f)
                {
                    Deki::Time::Delay(1);  // Yield so the watchdog on a device is fed
                }
                return f;
            },
            [](void* a) { delete static_cast<BitmapFont*>(a); },
            [](const uint8_t* d, size_t s) -> void* { return BitmapFont::LoadFromFileData(d, s); });
        // "Font" is an alias
        Deki::AssetManager::RegisterLoader(
            "Font",
            [](const char* p) -> void*
            {
                auto* f = BitmapFont::Load(p);
                if (f)
                {
                    Deki::Time::Delay(1);
                }
                return f;
            },
            [](void* a) { delete static_cast<BitmapFont*>(a); },
            [](const uint8_t* d, size_t s) -> void* { return BitmapFont::LoadFromFileData(d, s); });
    }
};
static FontLoaderReg s_FontLoaderReg;
}  // namespace

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

}  // namespace Deki2D
