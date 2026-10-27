#include "TextComponent.h"
#include <deki/providers/Memory.h>
#include "PixelFormat.h"
#include <deki/Object.h>
#include <deki/Engine.h>
#include "deki-rendering/CameraComponent.h"
#include <deki/LogSystem.h>
#include "deki-rendering/QuadBlit.h"
#include <deki/profiling/Profiler.h>
#include "Sprite.h"  // for reading chroma-key fields off the font atlas
#include "TextCrop.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <iomanip>
#include <unordered_map>

namespace Deki2D
{

// Set by the editor to handle GUID sync, preview and baking.
TextComponent::FontResolveCallback TextComponent::s_FontResolveCallback = nullptr;
void TextComponent::SetFontResolveCallback(FontResolveCallback cb)
{
    s_FontResolveCallback = cb;
}

// ============================================================================
// Component Registration
// ============================================================================
// s_Properties[] and s_ComponentMeta are generated into TextComponent.gen.h.

TextComponent::TextComponent()
    : DekiRendering::RendererComponent(),
      color(255, 255, 255, 255),
      decorationColor(0, 0, 0, 255)
{
}

TextComponent::~TextComponent()
{
    m_CachedBuffer.Reset();
}

void TextComponent::UnloadAssets()
{
    // Clearing the pointer and the load flag makes the font load again next
    // frame. The editor's re-bake relies on it:
    // 1. User changes font settings (e.g., No Antialiasing) and clicks Apply & Bake
    // 2. Editor calls InvalidateAllAssets(), so UnloadAssets() on all components
    // 3. The cleared pointer and flag resolve the reference again next frame
    // 4. AssetManager loads the fresh .dfont file with updated glyph data
    font.ptr = nullptr;
    font.loadAttempted = false;
    InvalidateRenderCache();
}

void TextComponent::InvalidateRenderCache()
{
    m_CachedBuffer.Reset();
}

QuadBlit::Source TextComponent::CroppedSource(int32_t bakeWidth) const
{
    const int32_t bpp = m_CachedAlphaOnly ? 1 : 3;
    QuadBlit::Source source = QuadBlit::MakeSource(
        m_CachedBuffer.Data() + (m_CropFirstRow * bakeWidth + m_CropFirstCol) * bpp, m_CropWidth, m_CropHeight,
        m_CachedAlphaOnly ? QuadBlit::PixelLayout::Alpha8() : QuadBlit::PixelLayout::RGB565A8(), false);
    source.stride = bakeWidth * bpp;
    return source;
}

void TextComponent::BakeTint(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const
{
    r = m_CachedAlphaOnly ? static_cast<uint8_t>(m_CachedColor.r & 0xF8) : 255;
    g = m_CachedAlphaOnly ? static_cast<uint8_t>(m_CachedColor.g & 0xFC) : 255;
    b = m_CachedAlphaOnly ? static_cast<uint8_t>(m_CachedColor.b & 0xF8) : 255;
    a = 255;
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

void TextComponent::SetColor(const Deki::Color& newColor)
{
    color = newColor;
    InvalidateRenderCache();
}

void TextComponent::SetColor(uint8_t r, uint8_t g, uint8_t b)
{
    color = Deki::Color(r, g, b, 255);
    InvalidateRenderCache();
}

int32_t TextComponent::GetTextWidth() const
{
    if (!font || text.empty())
    {
        return 0;
    }

    return font->MeasureWidth(text.c_str());
}

int32_t TextComponent::GetTextHeight() const
{
    if (!font || text.empty())
    {
        return 0;
    }

    // TODO: count the wrapped lines; this is one line's height.
    return font->GetLineHeight();
}

std::vector<std::string> TextComponent::WrapTextWithFont(const BitmapFont* fontPtr) const
{
    std::vector<std::string> result;

    // The width is in meters; glyphs are measured in pixels.
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t widthPx = static_cast<int32_t>(width * ppm);

    if (!fontPtr || text.empty() || widthPx <= 0)
    {
        if (!text.empty())
        {
            result.push_back(text);
        }
        return result;
    }

    // Bitmap fonts have no kerning, so a line's width is the sum of its words'
    // widths plus the spaces between them. Each word is measured once and
    // added; measuring the growing line again for every word would be
    // quadratic, with a string concatenation per step.
    const int32_t ps = (std::max)(int32_t{ 1 }, pixelScale);
    const int32_t spaceWidth = fontPtr->MeasureWidth(" ") * ps;

    std::string currentLine;
    size_t paraStart = 0;
    while (paraStart <= text.size())
    {
        // One paragraph per explicit newline.
        size_t paraEnd = text.find('\n', paraStart);
        if (paraEnd == std::string::npos)
        {
            paraEnd = text.size();
        }
        const std::string para = text.substr(paraStart, paraEnd - paraStart);
        paraStart = paraEnd + 1;
        if (paraEnd == text.size() && para.empty() && !result.empty())
        {
            break;  // trailing newline: no extra empty line
        }

        if (para.empty())
        {
            result.push_back("");
            continue;
        }

        // Whole paragraph fits: no wrapping needed.
        if (fontPtr->MeasureWidth(para.c_str()) * ps <= widthPx)
        {
            result.push_back(para);
            continue;
        }

        // Wrap word by word.
        currentLine.clear();
        int32_t currentLineWidth = 0;
        size_t wordStart = 0;
        while (wordStart < para.size())
        {
            // Skip runs of spaces; find the next word's extent.
            while (wordStart < para.size() && para[wordStart] == ' ')
            {
                ++wordStart;
            }
            if (wordStart >= para.size())
            {
                break;
            }
            size_t wordEnd = para.find(' ', wordStart);
            if (wordEnd == std::string::npos)
            {
                wordEnd = para.size();
            }
            const std::string word = para.substr(wordStart, wordEnd - wordStart);
            wordStart = wordEnd;

            const int32_t wordWidth = fontPtr->MeasureWidth(word.c_str()) * ps;
            const int32_t joinedWidth = currentLine.empty() ? wordWidth : currentLineWidth + spaceWidth + wordWidth;
            if (joinedWidth <= widthPx)
            {
                if (!currentLine.empty())
                {
                    currentLine += ' ';
                }
                currentLine += word;
                currentLineWidth = joinedWidth;
            }
            else
            {
                // Current line is full, start a new one.
                if (!currentLine.empty())
                {
                    result.push_back(currentLine);
                }
                if (wordWidth > widthPx)
                {
                    // A single word wider than the box goes on its own line.
                    result.push_back(word);
                    currentLine.clear();
                    currentLineWidth = 0;
                }
                else
                {
                    currentLine = word;
                    currentLineWidth = wordWidth;
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
    {
        return;
    }

    // width/height are world meters; the layout works in pixels.
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    float containerW = width * ppm;
    float containerH = height * ppm;

    std::vector<std::string> lines = WrapTextWithFont(fontPtr);

    float ps = static_cast<float>((std::max)(int32_t{ 1 }, pixelScale));

    float lineHeightF = static_cast<float>(fontPtr->GetLineHeight()) * ps;
    float totalTextHeight = lineHeightF * static_cast<float>(lines.size());

    // Visual bounds, for the ascender and descender.
    int32_t minY = 0, maxY = 0;
    fontPtr->GetVisualBounds(minY, maxY);
    float ascenderHeight = static_cast<float>(-minY) * ps;
    float descenderDepth = static_cast<float>(maxY) * ps;
    float visualLineHeight = ascenderHeight + descenderDepth;

    // Y of the first baseline, relative to the centre.
    float worldStartY = -containerH * 0.5f + ascenderHeight;

    switch (verticalAlign)
    {
        case TextVerticalAlign::Top: break;
        case TextVerticalAlign::Middle:
            // One line centres on its visual height, several on their total line height.
            if (lines.size() == 1)
            {
                worldStartY += (containerH - visualLineHeight) * 0.5f;
            }
            else
            {
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
            // The chosen anchor's offset from the baseline, in font pixels.
            // Positive is below the baseline.
            float anchorOffset = 0.0f;
            if (verticalAlign == TextVerticalAlign::CapCenter)
            {
                anchorOffset = -static_cast<float>(fontPtr->GetCapHeight()) * 0.5f;
            }
            else if (verticalAlign == TextVerticalAlign::XCenter)
            {
                anchorOffset = -static_cast<float>(fontPtr->GetXHeight()) * 0.5f;
            }
            else if (verticalAlign == TextVerticalAlign::TypoCenter)
            {
                anchorOffset = static_cast<float>(minY + maxY) * 0.5f;
            }
            // Baseline: anchorOffset stays 0

            // Place the baselines so the block of lines is centred with the anchor at 0.
            const float totalLineSpan = lineHeightF * (static_cast<float>(lines.size()) - 1.0f);
            worldStartY = -anchorOffset * ps - totalLineSpan * 0.5f;
            break;
        }
    }

    float worldLineY = worldStartY;
    for (const auto& lineText : lines)
    {
        if (lineText.empty())
        {
            worldLineY += lineHeightF;
            continue;
        }

        float lineWidth = static_cast<float>(fontPtr->MeasureWidth(lineText.c_str())) * ps;

        // Horizontal alignment, relative to the centre.
        float worldLineX = -containerW * 0.5f;
        switch (align)
        {
            case TextAlign::Center: worldLineX += (containerW - lineWidth) * 0.5f; break;
            case TextAlign::Right: worldLineX += containerW - lineWidth; break;
            case TextAlign::Left:
            default: break;
        }

        // Characters are decoded as UTF-8.
        float cursorX = 0;
        size_t ci = 0;
        size_t lineLen = lineText.length();
        const char* lineData = lineText.c_str();
        while (ci < lineLen)
        {
            uint32_t cp = BitmapFont::DecodeUtf8(lineData, lineLen, ci);
            const GlyphInfo* glyph = fontPtr->GetGlyphByCodepoint(cp);
            if (!glyph)
            {
                continue;
            }

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

bool TextComponent::RenderContent(const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX,
                                  float& outPivotY, uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB,
                                  uint8_t& outTintA)
{
    DEKI_PROFILE_SCOPE_N("TextComponent::RenderContent");
    if (!owner || text.empty())
    {
        return false;
    }

    // width/height are world meters; the bake works in pixels.
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t widthPx = static_cast<int32_t>(width * ppm);
    const int32_t heightPx = static_cast<int32_t>(height * ppm);

    if (widthPx <= 0 || heightPx <= 0)
    {
        return false;
    }

    BitmapFont* fontPtr = nullptr;

    // The editor's hook resolves the font first (GUID sync, preview, baking).
    if (s_FontResolveCallback)
    {
        fontPtr = s_FontResolveCallback(this);
    }

    // Otherwise, and at runtime, the AssetRef loads it.
    if (!fontPtr)
    {
        if (!font)
        {
            return false;
        }
        fontPtr = font.Get();
    }

    if (!fontPtr || !fontPtr->GetAtlas())
    {
        return false;
    }

    Deki::Texture2D* atlas = fontPtr->GetAtlas();
    if (!atlas->data)
    {
        return false;
    }

    bool cacheValid = static_cast<bool>(m_CachedBuffer) && m_CachedText == text && m_CachedWidth == widthPx &&
                      m_CachedHeight == heightPx && m_CachedColor == color &&
                      m_CachedDecorationColor == decorationColor && m_CachedAlign == align &&
                      m_CachedVerticalAlign == verticalAlign && m_CachedFont == fontPtr &&
                      m_CachedPixelScale == pixelScale;

    if (cacheValid)
    {
        outSource = CroppedSource(widthPx);
        outSource.pixelsPerMeter = ppm;
        outPivotX = m_CropPivotX;
        outPivotY = m_CropPivotY;
        BakeTint(outTintR, outTintG, outTintB, outTintA);
        return true;
    }

    // One colour: each pixel's alpha alone, a byte, tinted as it is drawn. A
    // decorated font's two colours: RGB565A8, 3 bytes.
    const bool alphaOnly = fontPtr->GetDecorationMode() == FontDecorationMode::None;
    const size_t bpp = alphaOnly ? 1 : 3;
    size_t bufferSize = widthPx * heightPx * bpp;

    // Allocate() leaves an unchanged size alone, so reuse costs nothing. The
    // size is the object's pixels times three, which a device may not have
    // room for; undrawn text beats a reboot.
    if (!m_CachedBuffer.Allocate(bufferSize, Deki::Memory::External))
    {
        DEKI_LOG_WARNING("TextComponent: no room for a %dx%d text bake (%u bytes); "
                         "not drawing it",
                         (int)widthPx, (int)heightPx, (unsigned)bufferSize);
        return false;
    }
    memset(m_CachedBuffer.Data(), 0, bufferSize);  // Clear to transparent
    ++m_BakeVersion;

    std::vector<GlyphLayout> glyphLayouts;
    CalculateGlyphLayout(fontPtr, glyphLayouts);

    uint32_t atlasBpp = Deki::Texture2D::GetBytesPerPixel(atlas->format);

    float centerX = widthPx * 0.5f;
    float centerY = heightPx * 0.5f;

    // Everything up to the glyph loop depends only on the font and the
    // component colours, so it is computed once per layout. The packed colours
    // are in memory's byte order, ready to store.
    const uint16_t textRgb565 = DekiPixel::ToMemory565(DekiPixel::PackRGB565(color.r, color.g, color.b));

    // Decoration palette, for v4 palette-indexed fonts. Entries:
    //   idx 0     = transparent (skipped)
    //   idx 1..4  = decoration edge AA: {64, 128, 192, 255}
    //   idx 5..15 = Outline: lerp(decoColor to fillColor), alpha 255
    //               Shadow: fillColor with alpha ramp 24..255
    FontDecorationMode decoMode = fontPtr->GetDecorationMode();
    const bool isPalette = (decoMode != FontDecorationMode::None);
    uint16_t palRgb[16] = { 0 };
    uint8_t palA[16] = { 0 };
    if (isPalette)
    {
        const uint16_t decoRgb =
            DekiPixel::ToMemory565(DekiPixel::PackRGB565(decorationColor.r, decorationColor.g, decorationColor.b));
        palRgb[0] = 0;
        palA[0] = 0;
        palRgb[1] = decoRgb;
        palA[1] = 64;
        palRgb[2] = decoRgb;
        palA[2] = 128;
        palRgb[3] = decoRgb;
        palA[3] = 192;
        palRgb[4] = decoRgb;
        palA[4] = 255;
        if (decoMode == FontDecorationMode::Outline)
        {
            for (int i = 5; i <= 15; ++i)
            {
                int t = i - 5;  // 0..10
                int mixR = (decorationColor.r * (10 - t) + color.r * t) / 10;
                int mixG = (decorationColor.g * (10 - t) + color.g * t) / 10;
                int mixB = (decorationColor.b * (10 - t) + color.b * t) / 10;
                palRgb[i] = DekiPixel::ToMemory565(DekiPixel::PackRGB565(
                    static_cast<uint8_t>(mixR), static_cast<uint8_t>(mixG), static_cast<uint8_t>(mixB)));
                palA[i] = 255;
            }
        }
        else  // Shadow
        {
            for (int i = 5; i <= 15; ++i)
            {
                palRgb[i] = textRgb565;
                palA[i] = static_cast<uint8_t>(24 + (i - 5) * 23);  // 24..254; index 15 is set to 255 below
                if (i == 15)
                {
                    palA[i] = 255;
                }
            }
        }
    }

    // The alpha byte's offset, decided once instead of per pixel.
    // -1 = opaque (no alpha channel), -2 = transparency color check
    int32_t alphaOffset;
    switch (atlas->format)
    {
        case Deki::Texture2D::TextureFormat::RGBA8888: alphaOffset = 3; break;
        case Deki::Texture2D::TextureFormat::RGB565A8: alphaOffset = 2; break;
        case Deki::Texture2D::TextureFormat::ALPHA8: alphaOffset = 0; break;
        default:
            alphaOffset = (atlas->hasTransparency && atlas->format == Deki::Texture2D::TextureFormat::RGB565) ? -2 : -1;
            break;
    }

    const uint8_t* atlasData = atlas->data;
    const int32_t atlasWidth = atlas->width;

    // Quantize the chroma key once. The atlas is always a Sprite at runtime.
    Sprite* atlasSprite = static_cast<Sprite*>(atlas);
    uint8_t keyR = 255, keyG = 0, keyB = 255;
    if (atlasSprite->hasChromaKey)
    {
        keyR = atlasSprite->transparentR;
        keyG = atlasSprite->transparentG;
        keyB = atlasSprite->transparentB;
        DekiPixel::QuantizeRGB565(keyR, keyG, keyB);
    }

    // Rows the glyphs actually touch, so the crop scan below reads only them.
    int32_t touchedFirstRow = heightPx;
    int32_t touchedLastRow = -1;

    for (const auto& layout : glyphLayouts)
    {
        if (!layout.glyph)
        {
            continue;
        }

        const GlyphInfo* glyph = layout.glyph;

        int32_t ps = (std::max)(int32_t{ 1 }, pixelScale);

        // Buffer position: the centre plus the glyph's offset, scaled.
        int32_t destX = static_cast<int32_t>(std::floor(centerX + layout.worldX)) + glyph->offsetX * ps;
        int32_t destY = static_cast<int32_t>(std::floor(centerY + layout.worldY)) + glyph->offsetY * ps;

        // Source rectangle in the atlas, unscaled.
        int32_t srcX = glyph->x;
        int32_t srcY = glyph->y;
        int32_t glyphW = glyph->width;
        int32_t glyphH = glyph->height;

        int32_t scaledW = glyphW * ps;
        int32_t scaledH = glyphH * ps;

        // Clip to the buffer, in scaled pixels.
        int32_t clipLeft = 0;
        int32_t clipTop = 0;
        int32_t clipRight = scaledW;
        int32_t clipBottom = scaledH;

        if (destX < 0)
        {
            clipLeft = -destX;
            destX = 0;
        }
        if (destY < 0)
        {
            clipTop = -destY;
            destY = 0;
        }
        if (destX + (clipRight - clipLeft) > widthPx)
        {
            clipRight = widthPx - destX + clipLeft;
        }
        if (destY + (clipBottom - clipTop) > heightPx)
        {
            clipBottom = heightPx - destY + clipTop;
        }

        if (clipLeft >= clipRight || clipTop >= clipBottom)
        {
            continue;
        }

        touchedFirstRow = (std::min)(touchedFirstRow, destY);
        touchedLastRow = (std::max)(touchedLastRow, destY + (clipBottom - clipTop) - 1);

        for (int32_t py = clipTop; py < clipBottom; py++)
        {
            int32_t atlasY = srcY + py / ps;
            int32_t bufY = destY + (py - clipTop);
            size_t atlasRow = atlasY * atlasWidth;
            size_t bufRow = bufY * widthPx;

            for (int32_t px = clipLeft; px < clipRight; px++)
            {
                int32_t atlasX = srcX + px / ps;
                int32_t bufX = destX + (px - clipLeft);

                size_t atlasOff = (atlasRow + atlasX) * atlasBpp;

                if (isPalette)
                {
                    // v4 palette font: the atlas byte's low nibble is a 0..15 index.
                    uint8_t idx = atlasData[atlasOff + (alphaOffset >= 0 ? alphaOffset : 0)] & 0x0F;
                    if (idx == 0)
                    {
                        continue;
                    }
                    size_t bufOffset = (bufRow + bufX) * 3;
                    DekiPixel::StoreU16(m_CachedBuffer.Data() + bufOffset, palRgb[idx]);
                    m_CachedBuffer[bufOffset + 2] = palA[idx];
                    continue;
                }

                uint8_t alpha;
                if (alphaOffset >= 0)
                {
                    alpha = atlasData[atlasOff + alphaOffset];
                }
                else if (alphaOffset == -2)
                {
                    // RGB565 atlas with a chroma key. Each font's key colour
                    // comes from its atlas Sprite.
                    uint8_t r, g, b;
                    DekiPixel::UnpackRGB565(DekiPixel::Load565(atlasData + atlasOff), r, g, b);
                    alpha = (r == keyR && g == keyG && b == keyB) ? 0 : 255;
                }
                else
                {
                    alpha = 255;
                }

                if (alpha == 0)
                {
                    continue;
                }

                if (alphaOnly)
                {
                    m_CachedBuffer[bufRow + bufX] = alpha;
                    continue;
                }
                // RGB565A8: 2 bytes RGB565 + 1 byte alpha.
                size_t bufOffset = (bufRow + bufX) * 3;
                DekiPixel::StoreU16(m_CachedBuffer.Data() + bufOffset, textRgb565);
                m_CachedBuffer[bufOffset + 2] = alpha;
            }
        }
    }

    m_CachedText = text;
    m_CachedWidth = widthPx;
    m_CachedHeight = heightPx;
    m_CachedColor = color;
    m_CachedDecorationColor = decorationColor;
    m_CachedAlign = align;
    m_CachedVerticalAlign = verticalAlign;
    m_CachedFont = fontPtr;
    m_CachedPixelScale = pixelScale;
    m_CachedAlphaOnly = alphaOnly;

    // Crop to the rows and columns that hold a pixel: a line of text in a
    // wide box is mostly empty columns, which every blit would read. Only
    // rows a glyph wrote to can hold one, so only those are scanned.
    int32_t firstRow = heightPx;
    int32_t lastRow = -1;
    int32_t firstCol = widthPx;
    int32_t lastCol = -1;
    const int32_t scanEnd = (std::min)(heightPx, touchedLastRow + 1);
    for (int32_t row = (std::max)(int32_t{ 0 }, touchedFirstRow); row < scanEnd; row++)
    {
        // The alpha byte: the only one, or the third.
        const uint8_t* alphaPtr = m_CachedBuffer.Data() + row * widthPx * bpp + (bpp - 1);
        int32_t col = 0;
        while (col < widthPx && alphaPtr[col * bpp] == 0)
        {
            col++;
        }
        if (col == widthPx)
        {
            continue;
        }
        int32_t last = widthPx - 1;
        while (alphaPtr[last * bpp] == 0)
        {
            last--;
        }
        firstRow = (std::min)(firstRow, row);
        lastRow = row;
        firstCol = (std::min)(firstCol, col);
        lastCol = (std::max)(lastCol, last);
    }

    if (lastRow < firstRow)
    {
        // Nothing drawn: use the whole buffer.
        firstRow = 0;
        lastRow = heightPx - 1;
        firstCol = 0;
        lastCol = widthPx - 1;
    }
    else
    {
        // Keep an empty column each side. A scaled blit rounds its edges
        // apart from where it samples, and with the crop flush to the glyphs
        // that rounding fell on them: the first column grew a pixel and the
        // last lost one.
        firstCol = (std::max)(int32_t{ 0 }, firstCol - 1);
        lastCol = (std::min)(widthPx - 1, lastCol + 1);
    }

    m_CropFirstRow = firstRow;
    m_CropHeight = lastRow - firstRow + 1;
    m_CropFirstCol = firstCol;
    m_CropWidth = lastCol - firstCol + 1;

    // Move the pivot so the crop stays where it was in the world.
    m_CropPivotX = CropPivot(widthPx, m_CropFirstCol, m_CropWidth);
    m_CropPivotY = CropPivot(heightPx, m_CropFirstRow, m_CropHeight);

    // The component keeps the buffer; the caller does not own it.
    outSource = CroppedSource(widthPx);
    outSource.pixelsPerMeter = ppm;
    outPivotX = m_CropPivotX;
    outPivotY = m_CropPivotY;
    BakeTint(outTintR, outTintG, outTintB, outTintA);
    return true;
}

}  // namespace Deki2D
