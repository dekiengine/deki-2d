#include "BitmapFont.h"
#include "Sprite.h"
#include "../../providers/DekiFileSystemProvider.h"
#include "../../DekiLogSystem.h"
#include <cstring>

BitmapFont::BitmapFont()
    : atlas(nullptr)
    , glyphs(nullptr)
    , first_char(0)
    , last_char(0)
    , line_height(0)
    , baseline(0)
    , glyph_count(0)
{
}

BitmapFont::~BitmapFont()
{
    delete atlas;
    delete[] glyphs;
}

BitmapFont* BitmapFont::Load(const char* file_path)
{
    if (!file_path)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: null file path");
        return nullptr;
    }

    // Read entire file
    IDekiFileSystem* fs = DekiFileSystemProvider::GetFileSystemForPath(file_path);
    if (!fs)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: No filesystem available for path: %s", file_path);
        return nullptr;
    }

    // Check if file exists
    if (!fs->FileExists(file_path))
    {
        DEKI_LOG_ERROR("BitmapFont::Load: File not found '%s'", file_path);
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

    // Validate version
    if (header.version != 1)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Unsupported version %u", header.version);
        delete[] file_data;
        return nullptr;
    }

    // Validate glyph count
    uint16_t expected_glyph_count = header.last_char - header.first_char + 1;
    if (header.glyph_count != expected_glyph_count)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Glyph count mismatch (got %u, expected %u)",
                       header.glyph_count, expected_glyph_count);
        delete[] file_data;
        return nullptr;
    }

    // Validate file size for glyph data
    size_t glyphs_size = header.glyph_count * sizeof(GlyphInfo);
    size_t min_size = sizeof(FontHeader) + glyphs_size + header.atlas_path_len;
    if (file_size < min_size)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: File too small for glyph data");
        delete[] file_data;
        return nullptr;
    }

    // Create font object
    BitmapFont* font = new BitmapFont();
    font->first_char = header.first_char;
    font->last_char = header.last_char;
    font->line_height = header.line_height;
    font->baseline = header.baseline;
    font->glyph_count = header.glyph_count;

    // Copy glyph data
    font->glyphs = new GlyphInfo[header.glyph_count];
    memcpy(font->glyphs, file_data + sizeof(FontHeader), glyphs_size);

    // Get atlas path (relative to font file)
    const char* atlas_rel_path = (const char*)(file_data + sizeof(FontHeader) + glyphs_size);

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

    // Load atlas texture
    font->atlas = Sprite::Load(atlas_path.c_str());
    if (!font->atlas)
    {
        DEKI_LOG_ERROR("BitmapFont::Load: Failed to load atlas '%s'", atlas_path.c_str());
        delete font;
        return nullptr;
    }

    DEKI_LOG_DEBUG("BitmapFont::Load: Loaded '%s' (%u glyphs, %dx%d atlas)",
                  file_path, font->glyph_count, font->atlas->width, font->atlas->height);

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

    DEKI_LOG_DEBUG("BitmapFont::CreateMonospace: Created font with %u glyphs (%dx%d)",
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

    DEKI_LOG_DEBUG("BitmapFont::CreateFromMemory: Created font with %u glyphs, line height %u",
                  font->glyph_count, line_height);

    return font;
}

const GlyphInfo* BitmapFont::GetGlyph(char c) const
{
    uint8_t code = static_cast<uint8_t>(c);
    if (code < first_char || code > last_char)
    {
        return nullptr;
    }
    return &glyphs[code - first_char];
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
    for (size_t i = 0; i < length; i++)
    {
        const GlyphInfo* glyph = GetGlyph(text[i]);
        if (glyph)
        {
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
