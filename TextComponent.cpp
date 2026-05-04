#include "TextComponent.h"
#include "DekiObject.h"
#include "DekiEngine.h"
#include "deki-rendering/CameraComponent.h"
#include "DekiLogSystem.h"
#include "deki-rendering/QuadBlit.h"
#include "profiling/DekiProfiler.h"
#include "Sprite.h"  // for reading chroma-key fields off the font atlas
#include <cstring>
#include <vector>
#include <sstream>
#include <iomanip>
#include <unordered_map>

// Decode one UTF-8 codepoint from str at position i, advance i past it.
// Returns the codepoint, or 0xFFFD on invalid sequence.
static uint32_t DecodeUtf8(const char* str, size_t len, size_t& i)
{
    uint8_t b0 = static_cast<uint8_t>(str[i]);
    if (b0 < 0x80)
    {
        i += 1;
        return b0;
    }
    else if ((b0 & 0xE0) == 0xC0 && i + 1 < len)
    {
        uint32_t cp = (b0 & 0x1F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    else if ((b0 & 0xF0) == 0xE0 && i + 2 < len)
    {
        uint32_t cp = (b0 & 0x0F) << 12;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    else if ((b0 & 0xF8) == 0xF0 && i + 3 < len)
    {
        uint32_t cp = (b0 & 0x07) << 18;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F) << 12;
        cp |= (static_cast<uint8_t>(str[i + 2]) & 0x3F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    i += 1; // Skip invalid byte
    return 0xFFFD;
}

// Font resolve callback — set by editor to handle GUID sync, preview, baking
TextComponent::FontResolveCallback TextComponent::s_fontResolveCallback = nullptr;
void TextComponent::SetFontResolveCallback(FontResolveCallback cb) { s_fontResolveCallback = cb; }


// ============================================================================
// Component Registration
// ============================================================================
// NOTE: s_Properties[] and s_ComponentMeta are now auto-generated in
// TextComponent.gen.h (included at end of TextComponent.h)

TextComponent::TextComponent()
    : RendererComponent(),
      color(255, 255, 255, 255),
      decorationColor(0, 0, 0, 255)
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

    // Pre-quantize chroma key once (out of the inner loop). The atlas is
    // always loaded via Sprite::Load, so the runtime type is Sprite — we
    // read the configured chroma color from there. Falls back to magenta
    // for sprites baked before chroma support existed (has_chroma_key=false).
    Sprite* atlasSprite = static_cast<Sprite*>(atlas);
    uint8_t key_r = 255, key_g = 0, key_b = 255;
    if (atlasSprite->has_chroma_key)
    {
        key_r = static_cast<uint8_t>((atlasSprite->transparent_r >> 3) << 3);
        key_g = static_cast<uint8_t>((atlasSprite->transparent_g >> 2) << 2);
        key_b = static_cast<uint8_t>((atlasSprite->transparent_b >> 3) << 3);
    }

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
                // RGB565 atlas with chroma-key transparency. Per-font chroma
                // color comes from the underlying Sprite (set via texture
                // inspector); we pre-quantized it above.
                uint16_t pixel = *((const uint16_t*)(atlas_data + atlas_off));
                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                uint8_t b = (pixel & 0x1F) << 3;
                alpha = (r == key_r && g == key_g && b == key_b) ? 0 : 255;
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

// Helper to measure width of a string (UTF-8 aware)
int32_t TextComponent::MeasureLineWidth(const char* str, size_t len) const
{
    if (!font || !str || len == 0)
        return 0;

    int32_t totalWidth = 0;
    size_t i = 0;
    while (i < len)
    {
        uint32_t cp = DecodeUtf8(str, len, i);
        const GlyphInfo* glyph = font->GetGlyphByCodepoint(cp);
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

        // Check if entire paragraph fits (account for pixel scale)
        int32_t ps = (std::max)(1, pixelScale);
        int32_t paraWidth = fontPtr->MeasureWidth(para.c_str()) * ps;
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
            int32_t testWidth = fontPtr->MeasureWidth(testLine.c_str()) * ps;

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
                int32_t wordWidth = fontPtr->MeasureWidth(word.c_str()) * ps;
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

    // Pixel scale factor
    float ps = static_cast<float>((std::max)(1, pixelScale));

    // Calculate total text height
    float lineHeightF = static_cast<float>(fontPtr->GetLineHeight()) * ps;
    float totalTextHeight = lineHeightF * static_cast<float>(lines.size());

    // Get visual bounds for ascender/descender
    int32_t minY = 0, maxY = 0;
    fontPtr->GetVisualBounds(minY, maxY);
    float ascenderHeight = static_cast<float>(-minY) * ps;
    float descenderDepth = static_cast<float>(maxY) * ps;
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
        case TextVerticalAlign::CapCenter:
        case TextVerticalAlign::XCenter:
        case TextVerticalAlign::TypoCenter:
        case TextVerticalAlign::Baseline:
        {
            // Anchor-based centering: offset-from-baseline of the chosen anchor,
            // in font-space pixels. Positive values mean below baseline.
            float anchorOffset = 0.0f;
            if (verticalAlign == TextVerticalAlign::CapCenter)
                anchorOffset = -static_cast<float>(fontPtr->GetCapHeight()) * 0.5f;
            else if (verticalAlign == TextVerticalAlign::XCenter)
                anchorOffset = -static_cast<float>(fontPtr->GetXHeight()) * 0.5f;
            else if (verticalAlign == TextVerticalAlign::TypoCenter)
                anchorOffset = static_cast<float>(minY + maxY) * 0.5f;
            // Baseline: anchorOffset stays 0

            // Baseline world-Y so the group of baselines is centered with anchor at 0.
            const float totalLineSpan = lineHeightF * (static_cast<float>(lines.size()) - 1.0f);
            worldStartY = -anchorOffset * ps - totalLineSpan * 0.5f;
            break;
        }
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

        // Measure line width (scaled)
        float lineWidth = static_cast<float>(fontPtr->MeasureWidth(lineText.c_str())) * ps;

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

        // Process each character (UTF-8 aware)
        float cursorX = 0;
        size_t ci = 0;
        size_t lineLen = lineText.length();
        const char* lineData = lineText.c_str();
        while (ci < lineLen)
        {
            uint32_t cp = DecodeUtf8(lineData, lineLen, ci);
            const GlyphInfo* glyph = fontPtr->GetGlyphByCodepoint(cp);
            if (!glyph)
                continue;

            // Store glyph layout
            GlyphLayout layout;
            layout.glyph = glyph;
            layout.worldX = worldLineX + cursorX;
            layout.worldY = worldLineY;
            outGlyphs.push_back(layout);

            cursorX += glyph->advance * ps;
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

    // Editor hook: let external code handle font resolution (GUID sync, preview, baking)
    if (s_fontResolveCallback)
        fontPtr = s_fontResolveCallback(this);

    // Runtime path: direct load from AssetRef
    if (!fontPtr)
    {
        if (!font) return false;
        fontPtr = font.Get();
    }

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
        && m_cachedDecorationColor == decorationColor
        && m_cachedAlign == align
        && m_cachedVerticalAlign == verticalAlign
        && m_cachedFont == fontPtr
        && m_cachedPixelScale == pixelScale;

    if (cacheValid)
    {
        outSource = QuadBlit::MakeSource(
            m_cachedBuffer + m_cropFirstRow * width * 3,
            width, m_cropHeight, 3, true, true, false);
        outSource.pixelsPerMeter = DekiEngineSettings::Global().pixelsPerMeter;
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

        int32_t ps = (std::max)(1, pixelScale);

        // Convert world position to buffer position (center + world offset, scaled)
        int32_t dest_x = static_cast<int32_t>(std::floor(centerX + layout.worldX)) + glyph->offset_x * ps;
        int32_t dest_y = static_cast<int32_t>(std::floor(centerY + layout.worldY)) + glyph->offset_y * ps;

        // Calculate source rectangle in atlas (native size)
        int32_t src_x = glyph->x;
        int32_t src_y = glyph->y;
        int32_t glyph_w = glyph->width;
        int32_t glyph_h = glyph->height;

        // Scaled dimensions in buffer
        int32_t scaled_w = glyph_w * ps;
        int32_t scaled_h = glyph_h * ps;

        // Clip to buffer bounds (in scaled pixel space)
        int32_t clip_left = 0;
        int32_t clip_top = 0;
        int32_t clip_right = scaled_w;
        int32_t clip_bottom = scaled_h;

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

        // Decoration palette (v4 palette-indexed fonts). Built once per glyph — it
        // only depends on TextComponent colours + font's baked decoration mode, so
        // pulling it out further would just be a micro-opt. Entries:
        //   idx 0     = transparent (skipped)
        //   idx 1..4  = decoration edge AA: {64, 128, 192, 255}
        //   idx 5..15 = Outline: lerp(decoColor → fillColor), alpha 255
        //               Shadow: fillColor with alpha ramp 24..255
        FontDecorationMode decoMode = fontPtr->GetDecorationMode();
        const bool isPalette = (decoMode != FontDecorationMode::None);
        uint16_t pal_rgb[16] = {0};
        uint8_t  pal_a[16]   = {0};
        if (isPalette)
        {
            const uint16_t decoRgb = ((decorationColor.r >> 3) << 11) |
                                     ((decorationColor.g >> 2) <<  5) |
                                     (decorationColor.b >> 3);
            pal_rgb[0] = 0;           pal_a[0] = 0;
            pal_rgb[1] = decoRgb;     pal_a[1] =  64;
            pal_rgb[2] = decoRgb;     pal_a[2] = 128;
            pal_rgb[3] = decoRgb;     pal_a[3] = 192;
            pal_rgb[4] = decoRgb;     pal_a[4] = 255;
            if (decoMode == FontDecorationMode::Outline)
            {
                for (int i = 5; i <= 15; ++i)
                {
                    int t = i - 5;                          // 0..10
                    int mixR = (decorationColor.r * (10 - t) + color.r * t) / 10;
                    int mixG = (decorationColor.g * (10 - t) + color.g * t) / 10;
                    int mixB = (decorationColor.b * (10 - t) + color.b * t) / 10;
                    pal_rgb[i] = ((mixR >> 3) << 11) | ((mixG >> 2) << 5) | (mixB >> 3);
                    pal_a[i] = 255;
                }
            }
            else // Shadow
            {
                for (int i = 5; i <= 15; ++i)
                {
                    pal_rgb[i] = text_rgb565;
                    pal_a[i] = static_cast<uint8_t>(24 + (i - 5) * 23);  // 24..277 → clamps to 255 at top
                    if (i == 15) pal_a[i] = 255;
                }
            }
        }

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

        // Pre-quantize chroma key once. Atlas is always a Sprite at runtime.
        Sprite* atlasSprite = static_cast<Sprite*>(atlas);
        uint8_t key_r = 255, key_g = 0, key_b = 255;
        if (atlasSprite->has_chroma_key)
        {
            key_r = static_cast<uint8_t>((atlasSprite->transparent_r >> 3) << 3);
            key_g = static_cast<uint8_t>((atlasSprite->transparent_g >> 2) << 2);
            key_b = static_cast<uint8_t>((atlasSprite->transparent_b >> 3) << 3);
        }

        for (int32_t py = clip_top; py < clip_bottom; py++)
        {
            // Map scaled pixel back to atlas pixel
            int32_t atlas_y = src_y + py / ps;
            int32_t buf_y = dest_y + (py - clip_top);
            size_t atlas_row = atlas_y * atlas_width;
            size_t buf_row = buf_y * width;

            for (int32_t px = clip_left; px < clip_right; px++)
            {
                int32_t atlas_x = src_x + px / ps;
                int32_t buf_x = dest_x + (px - clip_left);

                // Get pixel from atlas
                size_t atlas_off = (atlas_row + atlas_x) * atlas_bpp;

                if (isPalette)
                {
                    // V4 palette path: atlas byte's low nibble is a 0..15 index.
                    uint8_t idx = atlas_data[atlas_off + (alpha_offset >= 0 ? alpha_offset : 0)] & 0x0F;
                    if (idx == 0)
                        continue;
                    size_t buf_offset = (buf_row + buf_x) * 3;
                    *(uint16_t*)(m_cachedBuffer + buf_offset) = pal_rgb[idx];
                    m_cachedBuffer[buf_offset + 2] = pal_a[idx];
                    continue;
                }

                uint8_t alpha;
                if (alpha_offset >= 0)
                {
                    alpha = atlas_data[atlas_off + alpha_offset];
                }
                else if (alpha_offset == -2)
                {
                    // RGB565 atlas with chroma-key transparency. Per-font
                    // chroma color comes from the underlying Sprite.
                    uint16_t pixel = *((const uint16_t*)(atlas_data + atlas_off));
                    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                    uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                    uint8_t b = (pixel & 0x1F) << 3;
                    alpha = (r == key_r && g == key_g && b == key_b) ? 0 : 255;
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
    m_cachedDecorationColor = decorationColor;
    m_cachedAlign = align;
    m_cachedVerticalAlign = verticalAlign;
    m_cachedFont = fontPtr;
    m_cachedPixelScale = pixelScale;

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
    outSource.pixelsPerMeter = DekiEngineSettings::Global().pixelsPerMeter;
    outPivotX = 0.5f;
    outPivotY = m_cropPivotY;
    // Text color is baked into buffer - no additional tint
    outTintR = 255;
    outTintG = 255;
    outTintB = 255;
    outTintA = 255;
    return true;
}
