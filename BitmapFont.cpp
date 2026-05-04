#include "BitmapFont.h"
#include "Sprite.h"
#include "providers/DekiFileSystemProvider.h"
#include "DekiLogSystem.h"
#include "DekiTime.h"
#include "assets/AssetManager.h"
#include "assets/AssetPackReader.h"
#include <cstring>
 
BitmapFont::BitmapFont()
    : atlas(nullptr)
    , glyphs(nullptr)
    , codepoints(nullptr)
    , first_char(0)
    , last_char(0)
    , line_height(0)
    , baseline(0)
    , cap_height(0)
    , x_height(0)
    , decoration_mode(0)
    , decoration_a(0)
    , decoration_b(0)
    , glyph_count(0)
    , is_sparse(false)
{
}

BitmapFont::~BitmapFont()
{
    delete atlas;
    delete[] glyphs;
    delete[] codepoints;
}

BitmapFont* BitmapFont::Load(const char* file_path)
{
    if (!file_path)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: null file path");
        return nullptr;
    }

    uint32_t tStart = DekiTime::GetTime();

    // Read entire file
    IDekiFileSystem* fs = DekiFileSystemProvider::GetFileSystemForPath(file_path);
    if (!fs)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: No filesystem available for path: %s", file_path);
        return nullptr;
    }

    // Open file
    IDekiFileSystem::FileHandle file = fs->OpenFile(file_path, IDekiFileSystem::OpenMode::READ_BINARY);
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
    uint8_t* file_data = new uint8_t[file_size];
    size_t bytes_read = fs->ReadFile(file, file_data, file_size);
    fs->CloseFile(file);

    if (bytes_read != static_cast<size_t>(file_size))
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Failed to read file '%s'", file_path);
        delete[] file_data;
        return nullptr;
    }

    // Validate minimum size
    if (static_cast<size_t>(file_size) < sizeof(FontHeader))
    {
        DEKI_LOG_ERROR("BitmapFont::Load: File too small for header");
        delete[] file_data;
        return nullptr;
    }

    // Parse header
    FontHeader header;
    memcpy(&header, file_data, sizeof(FontHeader));

    // Validate magic
    if (memcmp(header.magic, "DFNT", 4) != 0)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Invalid magic (expected DFNT)");
        delete[] file_data;
        return nullptr;
    }

    // Validate version (low 31 bits — v3/v4 encode sparse flag in high bit)
    const uint32_t versionLow = header.version & 0x7FFFFFFFu;
    if (versionLow != 1 && versionLow != 2 && versionLow != 3 && versionLow != 4)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Unsupported version %u", header.version);
        delete[] file_data;
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
            delete[] file_data;
            delete font;
            return nullptr;
        }
        memcpy(&headerV4, file_data, sizeof(FontHeaderV4));
        const bool sparse = (headerV4.version & 0x80000000u) != 0;

        size_t codepoints_size = sparse ? headerV4.glyph_count * sizeof(uint32_t) : 0;
        size_t glyphs_size = headerV4.glyph_count * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV4) + codepoints_size + glyphs_size + headerV4.atlas_path_len;
        if (static_cast<size_t>(file_size) < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v4 glyph data");
            delete[] file_data;
            delete font;
            return nullptr;
        }

        font->first_char = headerV4.first_codepoint;
        font->last_char = headerV4.last_codepoint;
        font->line_height = headerV4.line_height;
        font->baseline = headerV4.baseline;
        font->cap_height = headerV4.cap_height;
        font->x_height = headerV4.x_height;
        font->decoration_mode = headerV4.decoration_mode;
        font->decoration_a = headerV4.decoration_a;
        font->decoration_b = headerV4.decoration_b;
        font->glyph_count = headerV4.glyph_count;
        font->is_sparse = sparse;

        size_t offset = sizeof(FontHeaderV4);
        if (sparse)
        {
            font->codepoints = new uint32_t[headerV4.glyph_count];
            memcpy(font->codepoints, file_data + offset, codepoints_size);
            offset += codepoints_size;
        }

        font->glyphs = new GlyphInfo[headerV4.glyph_count];
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
            delete[] file_data;
            delete font;
            return nullptr;
        }
        memcpy(&headerV3, file_data, sizeof(FontHeaderV3));
        const bool sparse = (headerV3.version & 0x80000000u) != 0;

        size_t codepoints_size = sparse ? headerV3.glyph_count * sizeof(uint32_t) : 0;
        size_t glyphs_size = headerV3.glyph_count * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV3) + codepoints_size + glyphs_size + headerV3.atlas_path_len;
        if (static_cast<size_t>(file_size) < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v3 glyph data");
            delete[] file_data;
            delete font;
            return nullptr;
        }

        font->first_char = headerV3.first_codepoint;
        font->last_char = headerV3.last_codepoint;
        font->line_height = headerV3.line_height;
        font->baseline = headerV3.baseline;
        font->cap_height = headerV3.cap_height;
        font->x_height = headerV3.x_height;
        font->glyph_count = headerV3.glyph_count;
        font->is_sparse = sparse;

        size_t offset = sizeof(FontHeaderV3);
        if (sparse)
        {
            font->codepoints = new uint32_t[headerV3.glyph_count];
            memcpy(font->codepoints, file_data + offset, codepoints_size);
            offset += codepoints_size;
        }

        font->glyphs = new GlyphInfo[headerV3.glyph_count];
        memcpy(font->glyphs, file_data + offset, glyphs_size);
        offset += glyphs_size;

        atlas_rel_path = (const char*)(file_data + offset);
    }
    else if (versionLow == 1)
    {
        // V1: contiguous ASCII glyph array
        uint16_t expected_glyph_count = header.last_char - header.first_char + 1;
        if (header.glyph_count != expected_glyph_count)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: Glyph count mismatch (got %u, expected %u)",
                           header.glyph_count, expected_glyph_count);
            delete[] file_data;
            delete font;
            return nullptr;
        }

        size_t glyphs_size = header.glyph_count * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeader) + glyphs_size + header.atlas_path_len;
        if (static_cast<size_t>(file_size) < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v1 glyph data");
            delete[] file_data;
            delete font;
            return nullptr;
        }

        font->first_char = header.first_char;
        font->last_char = header.last_char;
        font->line_height = header.line_height;
        font->baseline = header.baseline;
        font->glyph_count = header.glyph_count;
        font->is_sparse = false;

        font->glyphs = new GlyphInfo[header.glyph_count];
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
            delete[] file_data;
            delete font;
            return nullptr;
        }
        memcpy(&headerV2, file_data, sizeof(FontHeaderV2));

        size_t codepoints_size = headerV2.glyph_count * sizeof(uint32_t);
        size_t glyphs_size = headerV2.glyph_count * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV2) + codepoints_size + glyphs_size + headerV2.atlas_path_len;
        if (static_cast<size_t>(file_size) < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::Load: File too small for v2 glyph data");
            delete[] file_data;
            delete font;
            return nullptr;
        }

        font->first_char = headerV2.first_codepoint;
        font->last_char = headerV2.last_codepoint;
        font->line_height = headerV2.line_height;
        font->baseline = headerV2.baseline;
        font->glyph_count = headerV2.glyph_count;
        font->is_sparse = true;

        font->codepoints = new uint32_t[headerV2.glyph_count];
        memcpy(font->codepoints, file_data + sizeof(FontHeaderV2), codepoints_size);

        font->glyphs = new GlyphInfo[headerV2.glyph_count];
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

    delete[] file_data;

    // Defer atlas loading to first GetAtlas() call for faster prefab transitions
    font->atlasPath_ = atlas_path;
    font->atlas = nullptr;

    uint32_t tEnd = DekiTime::GetTime();
    DEKI_LOG_INFO("[PERF] BitmapFont::Load: %ums '%s' (%u glyphs, atlas deferred)",
                  tEnd - tStart, file_path, font->glyph_count);

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

        size_t codepoints_size = sparse ? headerV4.glyph_count * sizeof(uint32_t) : 0;
        size_t glyphs_size = headerV4.glyph_count * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV4) + codepoints_size + glyphs_size + headerV4.atlas_path_len;
        if (size < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v4 glyphs");
            delete font;
            return nullptr;
        }

        font->first_char = headerV4.first_codepoint;
        font->last_char = headerV4.last_codepoint;
        font->line_height = headerV4.line_height;
        font->baseline = headerV4.baseline;
        font->cap_height = headerV4.cap_height;
        font->x_height = headerV4.x_height;
        font->decoration_mode = headerV4.decoration_mode;
        font->decoration_a = headerV4.decoration_a;
        font->decoration_b = headerV4.decoration_b;
        font->glyph_count = headerV4.glyph_count;
        font->is_sparse = sparse;

        size_t offset = sizeof(FontHeaderV4);
        if (sparse)
        {
            font->codepoints = new uint32_t[headerV4.glyph_count];
            memcpy(font->codepoints, data + offset, codepoints_size);
            offset += codepoints_size;
        }
        font->glyphs = new GlyphInfo[headerV4.glyph_count];
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

        size_t codepoints_size = sparse ? headerV3.glyph_count * sizeof(uint32_t) : 0;
        size_t glyphs_size = headerV3.glyph_count * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV3) + codepoints_size + glyphs_size + headerV3.atlas_path_len;
        if (size < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v3 glyphs");
            delete font;
            return nullptr;
        }

        font->first_char = headerV3.first_codepoint;
        font->last_char = headerV3.last_codepoint;
        font->line_height = headerV3.line_height;
        font->baseline = headerV3.baseline;
        font->cap_height = headerV3.cap_height;
        font->x_height = headerV3.x_height;
        font->glyph_count = headerV3.glyph_count;
        font->is_sparse = sparse;

        size_t offset = sizeof(FontHeaderV3);
        if (sparse)
        {
            font->codepoints = new uint32_t[headerV3.glyph_count];
            memcpy(font->codepoints, data + offset, codepoints_size);
            offset += codepoints_size;
        }
        font->glyphs = new GlyphInfo[headerV3.glyph_count];
        memcpy(font->glyphs, data + offset, glyphs_size);
        offset += glyphs_size;
        atlas_rel_path = (const char*)(data + offset);
    }
    else if (versionLow == 1)
    {
        uint16_t expected_glyph_count = header.last_char - header.first_char + 1;
        if (header.glyph_count != expected_glyph_count)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: glyph count mismatch");
            delete font;
            return nullptr;
        }

        size_t glyphs_size = header.glyph_count * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeader) + glyphs_size + header.atlas_path_len;
        if (size < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small");
            delete font;
            return nullptr;
        }

        font->first_char = header.first_char;
        font->last_char = header.last_char;
        font->line_height = header.line_height;
        font->baseline = header.baseline;
        font->glyph_count = header.glyph_count;
        font->is_sparse = false;

        font->glyphs = new GlyphInfo[header.glyph_count];
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

        size_t codepoints_size = headerV2.glyph_count * sizeof(uint32_t);
        size_t glyphs_size = headerV2.glyph_count * sizeof(GlyphInfo);
        size_t min_size = sizeof(FontHeaderV2) + codepoints_size + glyphs_size + headerV2.atlas_path_len;
        if (size < min_size)
        {
            DEKI_LOG_ERROR("BitmapFont::LoadFromFileData: data too small for v2 glyphs");
            delete font;
            return nullptr;
        }

        font->first_char = headerV2.first_codepoint;
        font->last_char = headerV2.last_codepoint;
        font->line_height = headerV2.line_height;
        font->baseline = headerV2.baseline;
        font->glyph_count = headerV2.glyph_count;
        font->is_sparse = true;

        font->codepoints = new uint32_t[headerV2.glyph_count];
        memcpy(font->codepoints, data + sizeof(FontHeaderV2), codepoints_size);

        font->glyphs = new GlyphInfo[headerV2.glyph_count];
        memcpy(font->glyphs, data + sizeof(FontHeaderV2) + codepoints_size, glyphs_size);

        atlas_rel_path = (const char*)(data + sizeof(FontHeaderV2) + codepoints_size + glyphs_size);
    }

    // Font loaded from pack — atlas path is relative, store as-is.
    font->atlasPath_ = std::string(atlas_rel_path);
    font->atlas = nullptr;

    DEKI_LOG_INFO("[PERF] BitmapFont::LoadFromFileData: %u glyphs (from pack, atlas deferred, v%s)",
                  font->glyph_count, font->is_sparse ? "2" : "1");
    return font;
}

BitmapFont* BitmapFont::CreateMonospace(const char* atlas_path,
                                        uint8_t glyph_width,
                                        uint8_t glyph_height,
                                        uint8_t first_char,
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
    font->first_char = first_char;
    font->last_char = first_char + char_count - 1;
    font->line_height = glyph_height;
    font->baseline = glyph_height;  // Baseline at bottom for simple fonts
    font->glyph_count = char_count;

    // Generate glyph data
    font->glyphs = new GlyphInfo[char_count];
    for (uint8_t i = 0; i < char_count; i++)
    {
        uint8_t row = i / chars_per_row;
        uint8_t col = i % chars_per_row;

        font->glyphs[i].x = col * glyph_width;
        font->glyphs[i].y = row * glyph_height;
        font->glyphs[i].width = glyph_width;
        font->glyphs[i].height = glyph_height;
        font->glyphs[i].offset_x = 0;
        font->glyphs[i].offset_y = 0;
        font->glyphs[i].advance = glyph_width;
    }

    DEKI_LOG_INTERNAL("BitmapFont::CreateMonospace: Created font with %u glyphs (%dx%d)",
                  char_count, glyph_width, glyph_height);

    return font;
}

BitmapFont* BitmapFont::CreateFromMemory(Texture2D* atlas,
                                          GlyphInfo* glyphs,
                                          uint8_t first_char,
                                          uint8_t last_char,
                                          uint8_t line_height,
                                          uint8_t baseline)
{
    if (!atlas || !glyphs || last_char < first_char)
    {
        DEKI_LOG_ERROR("BitmapFont::CreateFromMemory: Invalid parameters");
        return nullptr;
    }

    BitmapFont* font = new BitmapFont();
    font->atlas = atlas;
    font->glyphs = glyphs;
    font->first_char = first_char;
    font->last_char = last_char;
    font->line_height = line_height;
    font->baseline = baseline;
    font->glyph_count = last_char - first_char + 1;

    DEKI_LOG_INTERNAL("BitmapFont::CreateFromMemory: Created font with %u glyphs, line height %u",
                  font->glyph_count, line_height);

    return font;
}

Texture2D* BitmapFont::GetAtlas() const
{
    if (!atlas && !atlasPath_.empty())
    {
        uint32_t t0 = DekiTime::GetTime();

        auto& packReader = Deki::AssetPackReader::Instance();
        if (packReader.HasPackIndex() && packReader.IsInPack(atlasPath_))
        {
            const auto* packAsset = packReader.GetAsset(atlasPath_);
            if (packAsset && packAsset->ptr && packAsset->size > 0)
            {
                atlas = Sprite::LoadFromFileData(packAsset->ptr, packAsset->size);
            }
        }
        else
        {
            atlas = Sprite::Load(atlasPath_.c_str());
        }

        uint32_t t1 = DekiTime::GetTime();
        if (!atlas)
        {
            DEKI_LOG_ERROR("BitmapFont::GetAtlas: Failed to load atlas '%s'", atlasPath_.c_str());
        }
        else
        {
            DEKI_LOG_INFO("[PERF] BitmapFont::GetAtlas: %ums (lazy load) %s", t1 - t0, atlasPath_.c_str());
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
    if (!glyphs || glyph_count == 0)
        return nullptr;

    if (is_sparse && codepoints)
    {
        // Binary search in sorted codepoint table
        int lo = 0, hi = static_cast<int>(glyph_count) - 1;
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
        if (codepoint < first_char || codepoint > last_char)
            return nullptr;
        return &glyphs[codepoint - first_char];
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
    if (is_sparse)
    {
        // UTF-8 decode for sparse (v2) fonts
        size_t i = 0;
        while (i < length)
        {
            uint8_t b0 = static_cast<uint8_t>(text[i]);
            uint32_t cp;
            if (b0 < 0x80) { cp = b0; i += 1; }
            else if ((b0 & 0xE0) == 0xC0 && i + 1 < length) { cp = ((b0 & 0x1F) << 6) | (static_cast<uint8_t>(text[i+1]) & 0x3F); i += 2; }
            else if ((b0 & 0xF0) == 0xE0 && i + 2 < length) { cp = ((b0 & 0x0F) << 12) | ((static_cast<uint8_t>(text[i+1]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i+2]) & 0x3F); i += 3; }
            else if ((b0 & 0xF8) == 0xF0 && i + 3 < length) { cp = ((b0 & 0x07) << 18) | ((static_cast<uint8_t>(text[i+1]) & 0x3F) << 12) | ((static_cast<uint8_t>(text[i+2]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i+3]) & 0x3F); i += 4; }
            else { i += 1; continue; }

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
    if (!glyphs || glyph_count == 0)
    {
        min_y = 0;
        max_y = line_height;
        return;
    }

    // Find the actual bounds by examining all glyphs
    min_y = INT32_MAX;
    max_y = INT32_MIN;

    for (uint16_t i = 0; i < glyph_count; i++)
    {
        const GlyphInfo& g = glyphs[i];
        if (g.height == 0)
            continue;

        // offset_y is relative to baseline, positive means below baseline
        int32_t glyph_top = g.offset_y;
        int32_t glyph_bottom = g.offset_y + g.height;

        if (glyph_top < min_y)
            min_y = glyph_top;
        if (glyph_bottom > max_y)
            max_y = glyph_bottom;
    }

    // If no valid glyphs found, use defaults
    if (min_y == INT32_MAX)
    {
        min_y = 0;
        max_y = line_height;
    }
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
    // v1/v2 fonts set cap_height to 0; fall back to typographic approximation.
    if (cap_height != 0)
        return cap_height;
    return static_cast<uint8_t>((baseline * 7) / 10);
}

uint8_t BitmapFont::GetXHeight() const
{
    if (x_height != 0)
        return x_height;
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
                    if (f) DekiTime::Delay(1); // Yield for watchdog on embedded
                    return f;
                },
                [](void* a) { delete static_cast<BitmapFont*>(a); },
                [](const uint8_t* d, size_t s) -> void* { return BitmapFont::LoadFromFileData(d, s); });
            // Also register as "Font" (alias)
            Deki::AssetManager::RegisterLoader("Font",
                [](const char* p) -> void* {
                    auto* f = BitmapFont::Load(p);
                    if (f) DekiTime::Delay(1);
                    return f;
                },
                [](void* a) { delete static_cast<BitmapFont*>(a); },
                [](const uint8_t* d, size_t s) -> void* { return BitmapFont::LoadFromFileData(d, s); });
        }
    };
    static _FontLoaderReg s_fontLoaderReg;
}
