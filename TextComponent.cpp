#include "TextComponent.h"
#include "DekiObject.h"
#include "deki-rendering/CameraComponent.h"
#include "DekiLogSystem.h"
#include "deki-rendering/QuadBlit.h"
#include "profiling/DekiProfiler.h"
#include <cstring>
#include <vector>
#include <sstream>
#include <iomanip>
#include <unordered_map>

#ifdef DEKI_EDITOR
#include <deki-editor/AssetPipeline.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>  // For std::malloc
#include <nlohmann/json.hpp>
#include "providers/DekiMemoryProvider.h"  // For atlas data allocation
#include "editor/FontCompiler.h"  // For on-demand font compilation
#include "Guid.h"  // For GenerateDeterministicGuid
#endif

#ifdef DEKI_EDITOR
namespace {
    // Single preview font slot - only one preview font exists at a time
    // Automatically cleared when preview changes or is disabled
    static BitmapFont* s_PreviewFont = nullptr;
    static std::string s_PreviewFontGuid;
    static int s_PreviewFontSize = 0;

    /**
     * @brief Resolve TTF path from source GUID
     */
    static std::string ResolveTTFPath(const std::string& sourceGuid)
    {
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        if (!pipeline)
            return "";

        const DekiEditor::AssetInfo* info = pipeline->GetAssetInfoByGuid(sourceGuid);
        if (!info)
            return "";

        return (std::filesystem::path(pipeline->GetProjectPath()) / info->path).string();
    }

    /**
     * @brief Compile a font on-demand for preview mode
     *
     * Uses a single temporary slot — only one preview font exists at a time.
     * Cleared when preview font/size changes.
     */
    static BitmapFont* GetEditorFontVariant(const std::string& fontGuid, int fontSize, bool /*allowOnDemandCompile*/)
    {
        if (fontGuid.empty() || fontSize <= 0)
            return nullptr;

        // Return existing preview if it matches
        if (s_PreviewFont && s_PreviewFontGuid == fontGuid && s_PreviewFontSize == fontSize)
        {
            return s_PreviewFont;
        }

        // Clear old preview font (different font/size requested)
        if (s_PreviewFont)
        {
            delete s_PreviewFont;
            s_PreviewFont = nullptr;
            s_PreviewFontGuid.clear();
            s_PreviewFontSize = 0;
        }

        // Compile on-demand
        std::string ttfPath = ResolveTTFPath(fontGuid);
        if (ttfPath.empty() || !std::filesystem::exists(ttfPath))
        {
            DEKI_LOG_WARNING("TextComponent: TTF not found for %s", fontGuid.c_str());
            return nullptr;
        }

        DEKI_LOG_EDITOR("TextComponent: Compiling preview font %s @ %d px", fontGuid.c_str(), fontSize);

        Deki2D::FontCompiler::CompileOptions options;
        options.fontSize = fontSize;
        options.firstChar = 32;
        options.lastChar = 126;
        options.padding = 2;

        Deki2D::FontCompiler::CompileResult result;
        if (!Deki2D::FontCompiler::CompileTrueTypeFont(ttfPath, options, result))
        {
            DEKI_LOG_ERROR("TextComponent: Failed to compile preview %s @ %d px", fontGuid.c_str(), fontSize);
            return nullptr;
        }

        // Create preview font
        Texture2D* atlas = new Texture2D();
        atlas->width = result.atlasWidth;
        atlas->height = result.atlasHeight;
        atlas->format = Texture2D::TextureFormat::RGBA8888;
        atlas->has_alpha = true;
        atlas->has_transparency = true;

        size_t atlasSize = result.atlasWidth * result.atlasHeight * 4;
        if (DekiMemoryProvider::IsInitialized())
        {
            atlas->data = static_cast<uint8_t*>(DekiMemoryProvider::Allocate(atlasSize, false, "FontPreviewAtlas"));
            atlas->allocated_with_backend = true;
        }
        else
        {
            atlas->data = static_cast<uint8_t*>(std::malloc(atlasSize));
        }

        if (!atlas->data)
        {
            DEKI_LOG_ERROR("TextComponent: Failed to allocate preview atlas data");
            delete atlas;
            return nullptr;
        }
        memcpy(atlas->data, result.atlasRGBA.data(), atlasSize);

        GlyphInfo* glyphsCopy = new GlyphInfo[result.glyphs.size()];
        memcpy(glyphsCopy, result.glyphs.data(), result.glyphs.size() * sizeof(GlyphInfo));

        BitmapFont* font = BitmapFont::CreateFromMemory(
            atlas, glyphsCopy,
            result.firstChar, result.lastChar,
            result.lineHeight, result.baseline
        );

        if (font)
        {
            s_PreviewFont = font;
            s_PreviewFontGuid = fontGuid;
            s_PreviewFontSize = fontSize;
            DEKI_LOG_EDITOR("TextComponent: Created preview font %s @ %d px", fontGuid.c_str(), fontSize);
        }

        return font;
    }
} // anonymous namespace

namespace Deki2D {
    // Forward declaration - implemented in Deki2DModule.cpp
    void ClearPreviewTextureCache();

    void ClearPreviewFont()
    {
        if (s_PreviewFont)
        {
            delete s_PreviewFont;
            s_PreviewFont = nullptr;
        }
        s_PreviewFontGuid.clear();
        s_PreviewFontSize = 0;

        // Also clear GPU texture cache to keep in sync
        ClearPreviewTextureCache();
    }

    bool SetPreviewFontFromData(
        const std::string& sourceGuid,
        int fontSize,
        const uint8_t* atlasRGBA,
        uint32_t atlasWidth,
        uint32_t atlasHeight,
        const GlyphInfo* glyphs,
        size_t glyphCount,
        uint8_t firstChar,
        uint8_t lastChar,
        uint8_t lineHeight,
        uint8_t baseline)
    {
        if (!atlasRGBA || !glyphs || glyphCount == 0 || atlasWidth == 0 || atlasHeight == 0)
        {
            DEKI_LOG_ERROR("SetPreviewFontFromData: Invalid parameters");
            return false;
        }

        // Clear old preview if different
        if (s_PreviewFontGuid != sourceGuid || s_PreviewFontSize != fontSize)
        {
            ClearPreviewFont();
        }

        // Create Texture2D for atlas - allocated HERE in deki-2d.dll
        Texture2D* atlas = new Texture2D();
        atlas->width = atlasWidth;
        atlas->height = atlasHeight;
        atlas->format = Texture2D::TextureFormat::RGBA8888;
        atlas->has_alpha = true;
        atlas->has_transparency = true;

        // Allocate atlas data matching what Texture2D destructor expects
        // Destructor uses DekiMemoryProvider::Free if initialized, else std::free
        size_t atlasSize = atlasWidth * atlasHeight * 4;
        if (DekiMemoryProvider::IsInitialized())
        {
            atlas->data = static_cast<uint8_t*>(DekiMemoryProvider::Allocate(atlasSize, false, "FontPreviewAtlas"));
            atlas->allocated_with_backend = true;
        }
        else
        {
            atlas->data = static_cast<uint8_t*>(std::malloc(atlasSize));
        }

        if (!atlas->data)
        {
            DEKI_LOG_ERROR("SetPreviewFontFromData: Failed to allocate atlas data");
            delete atlas;
            return false;
        }
        memcpy(atlas->data, atlasRGBA, atlasSize);

        // Copy glyph info - allocated HERE in deki-2d.dll
        GlyphInfo* glyphsCopy = new GlyphInfo[glyphCount];
        memcpy(glyphsCopy, glyphs, glyphCount * sizeof(GlyphInfo));

        // Create BitmapFont - takes ownership of atlas and glyphsCopy
        BitmapFont* font = BitmapFont::CreateFromMemory(
            atlas, glyphsCopy,
            firstChar, lastChar,
            lineHeight, baseline
        );

        if (!font)
        {
            DEKI_LOG_ERROR("SetPreviewFontFromData: Failed to create BitmapFont");
            return false;
        }

        // Store as single preview font
        s_PreviewFont = font;
        s_PreviewFontGuid = sourceGuid;
        s_PreviewFontSize = fontSize;

        DEKI_LOG_EDITOR("TextComponent: Set preview font %s @ %d px", sourceGuid.c_str(), fontSize);
        return true;
    }

    bool HasPreviewFont(const std::string& sourceGuid, int fontSize)
    {
        return s_PreviewFont && s_PreviewFontGuid == sourceGuid && s_PreviewFontSize == fontSize;
    }

    BitmapFont* GetPreviewFont(const std::string& sourceGuid, int fontSize)
    {
        if (s_PreviewFont && s_PreviewFontGuid == sourceGuid && s_PreviewFontSize == fontSize)
            return s_PreviewFont;
        return nullptr;
    }
} // namespace Deki2D
#endif // DEKI_EDITOR

// ============================================================================
// Component Registration
// ============================================================================
// NOTE: s_Properties[] and s_ComponentMeta are now auto-generated in
// TextComponent.gen.h (included at end of TextComponent.h)

TextComponent::TextComponent()
    : RendererComponent(),
      color(255, 255, 255, 255)
{
}

TextComponent::~TextComponent()
{
    delete[] m_cachedBuffer;
    m_cachedBuffer = nullptr;
}

void TextComponent::UnloadAssets()
{
    // Clear the cached font pointer and load flag to force reload on next frame
    // This is essential for editor font re-baking workflow:
    // 1. User changes font settings (e.g., No Antialiasing) and clicks Apply & Bake
    // 2. Editor calls InvalidateAllAssets() → UnloadAssets() on all components
    // 3. Cleared ptr + loadAttempted triggers full re-resolution on next frame
    // 4. AssetManager loads the fresh .dfont file with updated glyph data
    font.ptr = nullptr;
    font.loadAttempted = false;
    InvalidateRenderCache();
}

void TextComponent::InvalidateRenderCache()
{
    delete[] m_cachedBuffer;
    m_cachedBuffer = nullptr;
    m_cachedBufferSize = 0;
}

void TextComponent::SetText(const char* newText)
{
    if (newText)
    {
        text = newText;
    }
    else
    {
        text.clear();
    }
    InvalidateRenderCache();
}

void TextComponent::SetText(const std::string& newText)
{
    text = newText;
    InvalidateRenderCache();
}

void TextComponent::SetFont(BitmapFont* f)
{
    font = f;
    InvalidateRenderCache();
}

void TextComponent::SetColor(const deki::Color& newColor)
{
    color = newColor;
    InvalidateRenderCache();
}

void TextComponent::SetColor(uint8_t r, uint8_t g, uint8_t b)
{
    color = deki::Color(r, g, b, 255);
    InvalidateRenderCache();
}

int32_t TextComponent::GetTextWidth() const
{
    if (!font || text.empty())
        return 0;

    return font->MeasureWidth(text.c_str());
}

int32_t TextComponent::GetTextHeight() const
{
    if (!font || text.empty())
        return 0;

    // TODO: Handle word wrapping for multi-line text
    return font->GetLineHeight();
}

void TextComponent::RenderGlyph(const GlyphInfo* glyph,
                                 int32_t x,
                                 int32_t y,
                                 uint8_t* render_buffer,
                                 int screen_width,
                                 int screen_height)
{
    if (!glyph || !font || !font->GetAtlas())
        return;

    Texture2D* atlas = font->GetAtlas();
    if (!atlas->data)
        return;

    // Apply glyph offset
    int32_t dest_x = x + glyph->offset_x;
    int32_t dest_y = y + glyph->offset_y;

    // Calculate source rectangle in atlas
    int32_t src_x = glyph->x;
    int32_t src_y = glyph->y;
    int32_t glyph_w = glyph->width;
    int32_t glyph_h = glyph->height;

    // Clip to screen bounds
    int32_t clip_left = 0;
    int32_t clip_top = 0;
    int32_t clip_right = glyph_w;
    int32_t clip_bottom = glyph_h;

    if (dest_x < 0)
    {
        clip_left = -dest_x;
        dest_x = 0;
    }
    if (dest_y < 0)
    {
        clip_top = -dest_y;
        dest_y = 0;
    }
    if (dest_x + (clip_right - clip_left) > screen_width)
    {
        clip_right = screen_width - dest_x + clip_left;
    }
    if (dest_y + (clip_bottom - clip_top) > screen_height)
    {
        clip_bottom = screen_height - dest_y + clip_top;
    }

    // Nothing to render
    if (clip_left >= clip_right || clip_top >= clip_bottom)
        return;

    // Get bytes per pixel for atlas format
    uint32_t atlas_bpp = Texture2D::GetBytesPerPixel(atlas->format);
    bool has_alpha = atlas->has_alpha ||
                     atlas->format == Texture2D::TextureFormat::RGBA8888 ||
                     atlas->format == Texture2D::TextureFormat::RGB565A8 ||
                     atlas->format == Texture2D::TextureFormat::ALPHA8;

    // Convert color to RGB565
    uint16_t text_color_565 = ((color.r >> 3) << 11) |
                               ((color.g >> 2) << 5) |
                               (color.b >> 3);

    // Pre-compute alpha offset based on format to avoid per-pixel switch
    // -1 = opaque (no alpha channel), -2 = transparency color check
    int32_t alpha_offset;
    switch (atlas->format)
    {
        case Texture2D::TextureFormat::RGBA8888:  alpha_offset = 3; break;
        case Texture2D::TextureFormat::RGB565A8:  alpha_offset = 2; break;
        case Texture2D::TextureFormat::ALPHA8:    alpha_offset = 0; break;
        default:
            alpha_offset = (atlas->has_transparency && atlas->format == Texture2D::TextureFormat::RGB565) ? -2 : -1;
            break;
    }

    // Cache frequently accessed members as locals
    const uint8_t* atlas_data = atlas->data;
    const int32_t atlas_width = atlas->width;
    const uint8_t col_r = color.r;
    const uint8_t col_g = color.g;
    const uint8_t col_b = color.b;

    // Render glyph pixels
    for (int32_t py = clip_top; py < clip_bottom; py++)
    {
        int32_t atlas_y = src_y + py;
        int32_t screen_y_pos = dest_y + (py - clip_top);
        uint16_t* dest_row = (uint16_t*)(render_buffer + screen_y_pos * screen_width * 2);

        for (int32_t px = clip_left; px < clip_right; px++)
        {
            int32_t atlas_x = src_x + px;
            int32_t screen_x_pos = dest_x + (px - clip_left);

            // Get pixel from atlas
            size_t atlas_off = (atlas_y * atlas_width + atlas_x) * atlas_bpp;
            uint8_t alpha;

            if (alpha_offset >= 0)
            {
                alpha = atlas_data[atlas_off + alpha_offset];
            }
            else if (alpha_offset == -2)
            {
                // RGB565 transparency color check
                uint16_t pixel = *((const uint16_t*)(atlas_data + atlas_off));
                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                uint8_t b = (pixel & 0x1F) << 3;
                alpha = (r > 240 && g < 16 && b > 240) ? 0 : 255;
            }
            else
            {
                alpha = 255;
            }

            // Skip fully transparent pixels
            if (alpha == 0)
                continue;

            uint16_t* dest_pixel = dest_row + screen_x_pos;

            if (alpha == 255)
            {
                // Fully opaque - just write the color
                *dest_pixel = text_color_565;
            }
            else
            {
                // Alpha blend
                uint16_t bg = *dest_pixel;
                uint8_t bg_r = ((bg >> 11) & 0x1F) << 3;
                uint8_t bg_g = ((bg >> 5) & 0x3F) << 2;
                uint8_t bg_b = (bg & 0x1F) << 3;

                uint8_t inv_alpha = 255 - alpha;
                uint8_t out_r = ((col_r * alpha + bg_r * inv_alpha) + 128) >> 8;
                uint8_t out_g = ((col_g * alpha + bg_g * inv_alpha) + 128) >> 8;
                uint8_t out_b = ((col_b * alpha + bg_b * inv_alpha) + 128) >> 8;

                *dest_pixel = ((out_r >> 3) << 11) | ((out_g >> 2) << 5) | (out_b >> 3);
            }
        }
    }
}

// Helper to measure width of a string
int32_t TextComponent::MeasureLineWidth(const char* str, size_t len) const
{
    if (!font || !str || len == 0)
        return 0;

    int32_t totalWidth = 0;
    for (size_t i = 0; i < len; i++)
    {
        const GlyphInfo* glyph = font->GetGlyph(str[i]);
        if (glyph)
        {
            totalWidth += glyph->advance;
        }
    }
    return totalWidth;
}

// Word-wrap text to fit within maxWidth, returns vector of line strings
std::vector<std::string> TextComponent::WrapText(int32_t maxWidth) const
{
    return WrapTextWithFont(font.Get());
}

std::vector<std::string> TextComponent::WrapTextWithFont(const BitmapFont* fontPtr) const
{
    std::vector<std::string> result;

    if (!fontPtr || text.empty() || width <= 0)
    {
        if (!text.empty())
            result.push_back(text);
        return result;
    }

    // First split by explicit newlines
    std::vector<std::string> paragraphs;
    std::istringstream stream(text);
    std::string paragraph;
    while (std::getline(stream, paragraph))
    {
        paragraphs.push_back(paragraph);
    }

    // Process each paragraph for word wrapping
    for (const auto& para : paragraphs)
    {
        if (para.empty())
        {
            result.push_back("");
            continue;
        }

        // Check if entire paragraph fits
        int32_t paraWidth = fontPtr->MeasureWidth(para.c_str());
        if (paraWidth <= width)
        {
            result.push_back(para);
            continue;
        }

        // Need to wrap - split into words
        std::vector<std::string> words;
        std::string currentWord;
        for (char c : para)
        {
            if (c == ' ')
            {
                if (!currentWord.empty())
                {
                    words.push_back(currentWord);
                    currentWord.clear();
                }
            }
            else
            {
                currentWord += c;
            }
        }
        if (!currentWord.empty())
        {
            words.push_back(currentWord);
        }

        // Build lines word by word
        std::string currentLine;
        for (const auto& word : words)
        {
            std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
            int32_t testWidth = fontPtr->MeasureWidth(testLine.c_str());

            if (testWidth <= width)
            {
                currentLine = testLine;
            }
            else
            {
                // Current line is full, start new line
                if (!currentLine.empty())
                {
                    result.push_back(currentLine);
                }
                // Check if single word exceeds width
                int32_t wordWidth = fontPtr->MeasureWidth(word.c_str());
                if (wordWidth > width)
                {
                    // Word is too long, add it anyway
                    result.push_back(word);
                    currentLine.clear();
                }
                else
                {
                    currentLine = word;
                }
            }
        }
        if (!currentLine.empty())
        {
            result.push_back(currentLine);
        }
    }

    return result;
}

void TextComponent::CalculateGlyphLayout(const BitmapFont* fontPtr, std::vector<GlyphLayout>& outGlyphs) const
{
    outGlyphs.clear();

    if (!fontPtr || text.empty())
        return;

    // Use float for all calculations
    float containerW = static_cast<float>(width);
    float containerH = static_cast<float>(height);

    // Word-wrap text
    std::vector<std::string> lines = WrapTextWithFont(fontPtr);

    // Calculate total text height
    float lineHeightF = static_cast<float>(fontPtr->GetLineHeight());
    float totalTextHeight = lineHeightF * static_cast<float>(lines.size());

    // Get visual bounds for ascender/descender
    int32_t minY = 0, maxY = 0;
    fontPtr->GetVisualBounds(minY, maxY);
    float ascenderHeight = static_cast<float>(-minY);
    float descenderDepth = static_cast<float>(maxY);
    float visualLineHeight = ascenderHeight + descenderDepth;

    // Calculate baseline Y position (relative to center)
    float worldStartY = -containerH * 0.5f + ascenderHeight;

    switch (verticalAlign)
    {
        case TextVerticalAlign::Top:
            break;
        case TextVerticalAlign::Middle:
            // Use visual bounds for single line, lineHeight for multi-line
            if (lines.size() == 1)
            {
                // Center single line based on actual visual height
                worldStartY += (containerH - visualLineHeight) * 0.5f;
            }
            else
            {
                // Multi-line: center total text block
                worldStartY += (containerH - totalTextHeight) * 0.5f;
            }
            break;
        case TextVerticalAlign::Bottom:
            worldStartY = containerH * 0.5f - descenderDepth - (static_cast<float>(lines.size()) - 1.0f) * lineHeightF;
            break;
    }

    // Process each line
    float worldLineY = worldStartY;
    for (const auto& lineText : lines)
    {
        if (lineText.empty())
        {
            worldLineY += lineHeightF;
            continue;
        }

        // Measure line width
        float lineWidth = static_cast<float>(fontPtr->MeasureWidth(lineText.c_str()));

        // Apply horizontal alignment (relative to center)
        float worldLineX = -containerW * 0.5f;
        switch (align)
        {
            case TextAlign::Center:
                worldLineX += (containerW - lineWidth) * 0.5f;
                break;
            case TextAlign::Right:
                worldLineX += containerW - lineWidth;
                break;
            case TextAlign::Left:
            default:
                break;
        }

        // Process each character
        float cursorX = 0;
        for (size_t i = 0; i < lineText.length(); i++)
        {
            char c = lineText[i];
            const GlyphInfo* glyph = fontPtr->GetGlyph(c);
            if (!glyph)
                continue;

            // Store glyph layout
            GlyphLayout layout;
            layout.glyph = glyph;
            layout.worldX = worldLineX + cursorX;
            layout.worldY = worldLineY;
            outGlyphs.push_back(layout);

            cursorX += glyph->advance;
        }

        worldLineY += lineHeightF;
    }
}

bool TextComponent::RenderContent(const DekiObject* owner,
                                   QuadBlit::Source& outSource,
                                   float& outPivotX,
                                   float& outPivotY,
                                   uint8_t& outTintR,
                                   uint8_t& outTintG,
                                   uint8_t& outTintB,
                                   uint8_t& outTintA)
{
    DEKI_PROFILE_SCOPE_N("TextComponent::RenderContent");
    if (!owner || text.empty())
        return false;

    if (width <= 0 || height <= 0)
        return false;

    BitmapFont* fontPtr = nullptr;
    bool usePreview = false;

#ifdef DEKI_EDITOR
    usePreview = (previewEnabled && previewSize > 0);
    if (usePreview)
    {
        // Preview mode: compile on-demand (editor-only)
        fontPtr = GetEditorFontVariant(font.source, previewSize, true);
    }
    else
    {
        // Sync font.guid with current fontSize so font.Get() loads the right variant
        if (!font.source.empty() && fontSize > 0)
        {
            std::string expectedGuid = Deki::GenerateDeterministicGuid(
                font.source + ":" + std::to_string(fontSize));
            if (font.guid != expectedGuid)
            {
                font.guid = expectedGuid;
                font.ptr = nullptr;
                font.loadAttempted = false;
            }
        }
    }
#endif

    // Unified path: same as embedded runtime
    if (!usePreview)
    {
        if (!font) return false;
        fontPtr = font.Get();
    }

#ifdef DEKI_EDITOR
    fontSizeUnavailable = (!usePreview && fontPtr == nullptr
                           && !font.source.empty() && fontSize > 0);
#endif
    if (!fontPtr || !fontPtr->GetAtlas())
        return false;

    Texture2D* atlas = fontPtr->GetAtlas();
    if (!atlas->data)
        return false;

    // Check if we can reuse the cached buffer
    bool cacheValid = m_cachedBuffer != nullptr
        && m_cachedText == text
        && m_cachedWidth == width
        && m_cachedHeight == height
        && m_cachedColor == color
        && m_cachedAlign == align
        && m_cachedVerticalAlign == verticalAlign
        && m_cachedFont == fontPtr;

    if (cacheValid)
    {
        outSource = QuadBlit::MakeSource(
            m_cachedBuffer + m_cropFirstRow * width * 3,
            width, m_cropHeight, 3, true, true, false);
        outPivotX = 0.5f;
        outPivotY = m_cropPivotY;
        outTintR = 255;
        outTintG = 255;
        outTintB = 255;
        outTintA = 255;
        return true;
    }

    // Allocate RGB565A8 buffer for text with alpha (3 bytes/pixel, faster blit than RGBA8888)
    size_t bufferSize = width * height * 3;

    // Reuse existing cache buffer if same size, otherwise reallocate
    if (m_cachedBufferSize != bufferSize)
    {
        delete[] m_cachedBuffer;
        m_cachedBuffer = new uint8_t[bufferSize];
        m_cachedBufferSize = bufferSize;
    }
    memset(m_cachedBuffer, 0, bufferSize);  // Clear to transparent

    // Calculate glyph layout using shared method
    std::vector<GlyphLayout> glyphLayouts;
    CalculateGlyphLayout(fontPtr, glyphLayouts);

    // Get bytes per pixel for atlas format
    uint32_t atlas_bpp = Texture2D::GetBytesPerPixel(atlas->format);

    // Center of our buffer in local coordinates
    float centerX = width * 0.5f;
    float centerY = height * 0.5f;

    // Render each glyph to the buffer
    for (const auto& layout : glyphLayouts)
    {
        if (!layout.glyph)
            continue;

        const GlyphInfo* glyph = layout.glyph;

        // Convert world position to buffer position (center + world offset)
        int32_t dest_x = static_cast<int32_t>(std::floor(centerX + layout.worldX)) + glyph->offset_x;
        int32_t dest_y = static_cast<int32_t>(std::floor(centerY + layout.worldY)) + glyph->offset_y;

        // Calculate source rectangle in atlas
        int32_t src_x = glyph->x;
        int32_t src_y = glyph->y;
        int32_t glyph_w = glyph->width;
        int32_t glyph_h = glyph->height;

        // Clip to buffer bounds
        int32_t clip_left = 0;
        int32_t clip_top = 0;
        int32_t clip_right = glyph_w;
        int32_t clip_bottom = glyph_h;

        if (dest_x < 0)
        {
            clip_left = -dest_x;
            dest_x = 0;
        }
        if (dest_y < 0)
        {
            clip_top = -dest_y;
            dest_y = 0;
        }
        if (dest_x + (clip_right - clip_left) > width)
        {
            clip_right = width - dest_x + clip_left;
        }
        if (dest_y + (clip_bottom - clip_top) > height)
        {
            clip_bottom = height - dest_y + clip_top;
        }

        // Nothing to render for this glyph
        if (clip_left >= clip_right || clip_top >= clip_bottom)
            continue;

        // Render glyph pixels to buffer (RGB565A8 — 3 bytes per pixel)
        // Pre-compute text color as RGB565
        uint16_t text_rgb565 = ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);

        // Pre-compute alpha offset based on format to avoid per-pixel switch
        // -1 = opaque (no alpha channel), -2 = transparency color check
        int32_t alpha_offset;
        switch (atlas->format)
        {
            case Texture2D::TextureFormat::RGBA8888:  alpha_offset = 3; break;
            case Texture2D::TextureFormat::RGB565A8:  alpha_offset = 2; break;
            case Texture2D::TextureFormat::ALPHA8:    alpha_offset = 0; break;
            default:
                alpha_offset = (atlas->has_transparency && atlas->format == Texture2D::TextureFormat::RGB565) ? -2 : -1;
                break;
        }

        const uint8_t* atlas_data = atlas->data;
        const int32_t atlas_width = atlas->width;

        for (int32_t py = clip_top; py < clip_bottom; py++)
        {
            int32_t atlas_y = src_y + py;
            int32_t buf_y = dest_y + (py - clip_top);
            size_t atlas_row = atlas_y * atlas_width;
            size_t buf_row = buf_y * width;

            for (int32_t px = clip_left; px < clip_right; px++)
            {
                int32_t atlas_x = src_x + px;
                int32_t buf_x = dest_x + (px - clip_left);

                // Get pixel from atlas
                size_t atlas_off = (atlas_row + atlas_x) * atlas_bpp;
                uint8_t alpha;

                if (alpha_offset >= 0)
                {
                    alpha = atlas_data[atlas_off + alpha_offset];
                }
                else if (alpha_offset == -2)
                {
                    // RGB565 transparency color check
                    uint16_t pixel = *((const uint16_t*)(atlas_data + atlas_off));
                    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                    uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                    uint8_t b = (pixel & 0x1F) << 3;
                    alpha = (r > 240 && g < 16 && b > 240) ? 0 : 255;
                }
                else
                {
                    alpha = 255;
                }

                // Skip fully transparent pixels
                if (alpha == 0)
                    continue;

                // Write to buffer (RGB565A8: 2 bytes RGB565 + 1 byte alpha)
                size_t buf_offset = (buf_row + buf_x) * 3;
                *(uint16_t*)(m_cachedBuffer + buf_offset) = text_rgb565;
                m_cachedBuffer[buf_offset + 2] = alpha;
            }
        }
    }

    // Update cache keys
    m_cachedText = text;
    m_cachedWidth = width;
    m_cachedHeight = height;
    m_cachedColor = color;
    m_cachedAlign = align;
    m_cachedVerticalAlign = verticalAlign;
    m_cachedFont = fontPtr;

    // Compute tight vertical crop bounds (scan for first/last non-empty row)
    int32_t firstRow = height;
    int32_t lastRow = -1;
    for (int32_t row = 0; row < height; row++)
    {
        const uint8_t* rowPtr = m_cachedBuffer + row * width * 3;
        for (int32_t col = 0; col < width; col++)
        {
            if (rowPtr[col * 3 + 2] != 0)  // Check alpha byte
            {
                if (row < firstRow) firstRow = row;
                lastRow = row;
                break;
            }
        }
    }

    if (lastRow < firstRow)
    {
        // Entirely empty — fall back to full buffer
        firstRow = 0;
        lastRow = height - 1;
    }

    m_cropFirstRow = firstRow;
    m_cropHeight = lastRow - firstRow + 1;

    // Adjust pivot so the cropped sub-region stays at the correct world position
    // Original pivot 0.5 corresponds to the center of the full height buffer.
    // We need pivotY such that: cropFirstRow + pivotY * cropHeight == 0.5 * height
    m_cropPivotY = (0.5f * height - static_cast<float>(m_cropFirstRow))
                 / static_cast<float>(m_cropHeight);

    // Return source - RGB565A8 format (not owned by caller, we manage lifetime)
    outSource = QuadBlit::MakeSource(
        m_cachedBuffer + m_cropFirstRow * width * 3,
        width, m_cropHeight, 3, true, true, false);
    outPivotX = 0.5f;
    outPivotY = m_cropPivotY;
    // Text color is baked into buffer - no additional tint
    outTintR = 255;
    outTintG = 255;
    outTintB = 255;
    outTintA = 255;
    return true;
}
