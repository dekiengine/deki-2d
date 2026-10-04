#pragma once

#include <deki/providers/Buffer.h>

#include <cstdint>
#include <string>
#include <vector>
#include "deki-rendering/RendererComponent.h"
#include "BitmapFont.h"
#include <deki/Color.h>
#include <deki/assets/AssetRef.h>

namespace Deki2D
{

enum class TextAlign : uint8_t
{
    Left = 0,
    Center = 1,
    Right = 2
};

/// Vertical text alignment. Top, Middle and Bottom use the font's full
/// ascent and descent. The other anchors use font-wide typographic metrics,
/// so text centres the same whether or not it has ascenders or descenders.
enum class TextVerticalAlign : uint8_t
{
    Top = 0,
    Middle = 1,  // centres on the whole font's visual bounds (ascent+descent)
    Bottom = 2,
    CapCenter = 3,   // centres on cap height; best for uppercase and mixed UI labels
    XCenter = 4,     // centres on x-height; best for mostly lowercase body text
    TypoCenter = 5,  // Centers on typographic midline; same as Middle for single-line
    Baseline = 6     // Baseline sits on the container center line
};

/// Draws text with a BitmapFont, with colour, alignment and word wrap.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Draws text with a bitmap font, alignment and word wrap.")
DEKI_FORMER_NAME("TextComponent")
class TextComponent : public DekiRendering::RendererComponent
{
public:
    TextComponent();
    virtual ~TextComponent();

    // ========================================================================
    // Editor-visible properties
    // ========================================================================

    DEKI_EXPORT
    DEKI_TOOLTIP(
        "The string to draw. Newlines start a new line, and with a width set, lines wrap between words to fit it.")
    std::string text;

    // The AssetRef stores the GUID and loads the font.
    DEKI_EXPORT
    DEKI_TOOLTIP("A bitmap font asset. Fonts are baked to a fixed size, so pick one close to the size you want rather "
                 "than scaling far from it.")
    Deki::AssetRef<BitmapFont> font;

#ifdef DEKI_EDITOR
    /// Font size in pixels; picks the baked variant of that size.
    DEKI_EXPORT
    DEKI_TOOLTIP(
        "Size used for the editor preview only. What the device draws is the size the font asset was baked at.")
    DEKI_EDITOR_ONLY
    int32_t fontSize = 16;

    /// Live font preview. Not serialized.
    bool previewEnabled = false;

    /// Font size to preview. Not serialized.
    int32_t previewSize = 16;

    /// True when no baked variant of fontSize exists.
    bool fontSizeUnavailable = false;

    /// What the editor's font-resolve hook last synced font.guid from. Not
    /// serialized. Working out the baked variant's GUID from (font.source,
    /// fontSize) takes a string build, a GUID hash and an asset-pipeline path
    /// lookup; these let the hook do that once per change instead of every
    /// frame for every text.
    std::string editorSyncedFontSource;
    int32_t editorSyncedFontSize = -1;
    std::string editorSyncedFontGuid;
#endif

    void SetText(const char* text);

    void SetText(const std::string& text);

    const std::string& GetText() const { return text; }

    /// Lets the editor resolve the font during RenderContent (GUID sync,
    /// preview, baking). The callback returns the font to use, or nullptr to
    /// fall back to font.Get().
    using FontResolveCallback = BitmapFont* (*)(TextComponent*);
    static void SetFontResolveCallback(FontResolveCallback cb);

    /// The component does not own the font.
    void SetFont(BitmapFont* f);

    /// The font, or nullptr.
    BitmapFont* GetFont() { return font.Get(); }
    const BitmapFont* GetFont() const { return font.Get(); }

    void SetColor(const Deki::Color& color);

    void SetColor(uint8_t r, uint8_t g, uint8_t b);

    const Deki::Color& GetColor() const { return color; }

    void SetAlign(TextAlign alignVal) { align = alignVal; }

    TextAlign GetAlign() const { return align; }

    void SetVerticalAlign(TextVerticalAlign alignVal) { verticalAlign = alignVal; }

    TextVerticalAlign GetVerticalAlign() const { return verticalAlign; }

    /// Text box width, in meters.
    void SetWidth(float w) { width = w; }

    float GetWidth() const { return width; }

    /// Text box height, in meters.
    void SetHeight(float h) { height = h; }

    float GetHeight() const { return height; }

    /// Width of the text on one line, in pixels.
    int32_t GetTextWidth() const;

    /// Height of the text in pixels. Counts one line only; wrapping is not included.
    int32_t GetTextHeight() const;

    // Drops the cached font so it loads again; the editor calls it after re-baking a font.
    void UnloadAssets() override;

    // Rendering through QuadBlit.
    // Culling extents (meters): the box the content is baked into.
    bool GetContentExtents(float& outWidth, float& outHeight) const override
    {
        if (width <= 0.0f || height <= 0.0f)
        {
            return false;
        }
        outWidth = width;
        outHeight = height;
        return true;
    }

    bool RenderContent(const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX, float& outPivotY,
                       uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB, uint8_t& outTintA) override;

    // ========================================================================
    // Layout methods (shared between runtime and editor)
    // ========================================================================

    struct GlyphLayout
    {
        const GlyphInfo* glyph;  // Glyph data from font
        float worldX;            // X position relative to component center
        float worldY;            // Y position relative to component center
    };

    /// Positions every glyph of the text, relative to the component's centre.
    /// `fontPtr` may differ from the component's font (the editor passes its
    /// own). The runtime uses the positions as they are; the editor scales
    /// them by its zoom.
    void CalculateGlyphLayout(const BitmapFont* fontPtr, std::vector<GlyphLayout>& outGlyphs) const;

    /// The text split into lines: at each newline, and word-wrapped to the
    /// box width as measured with `fontPtr`. Public for the editor.
    std::vector<std::string> WrapTextWithFont(const BitmapFont* fontPtr) const;

    // ========================================================================
    // Editor-visible properties (public for reflection)
    // ========================================================================

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the text box in meters. Alignment is measured against this, so it matters even when the "
                 "text is shorter.")
    DEKI_UNIT(Distance)
    float width = 6.25f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the text box in meters. Vertical alignment is measured against this.")
    DEKI_UNIT(Distance)
    float height = 1.5f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Colour of the glyphs.")
    Deki::Color color;

    /// Outline or shadow colour. Used only when the font (v4 or later) was
    /// baked with a decoration.
    DEKI_EXPORT
    DEKI_TOOLTIP("Colour of the outline or shadow, when the font asset was baked with one. Ignored by a plain font.")
    Deki::Color decorationColor;

    /// Whole-number nearest-neighbour magnification (1x, 2x, 3x).
    DEKI_EXPORT
    DEKI_TOOLTIP("Whole-number magnification. 2 draws every glyph pixel as a 2x2 block, which keeps a pixel font crisp "
                 "instead of blurring it.")
    int32_t pixelScale = 1;

    DEKI_EXPORT
    DEKI_TOOLTIP("Horizontal placement inside the width above.")
    TextAlign align = TextAlign::Left;

    /// Defaults to cap centre, which looks centred to the eye.
    DEKI_EXPORT
    DEKI_TOOLTIP("Vertical placement inside the height above. Cap-centre lines up the capital letters, which usually "
                 "looks centred to the eye; true centre includes descenders.")
    TextVerticalAlign verticalAlign = TextVerticalAlign::CapCenter;

    // Call when the text, font, colour or size changes.
    void InvalidateRenderCache();

private:
    static FontResolveCallback s_FontResolveCallback;

    // The baked text, RGB565A8. Owning, and it knows its own size.
    Deki::Buffer<uint8_t> m_CachedBuffer;
    std::string m_CachedText;
    int32_t m_CachedWidth = 0;
    int32_t m_CachedHeight = 0;
    Deki::Color m_CachedColor;
    Deki::Color m_CachedDecorationColor;
    TextAlign m_CachedAlign = TextAlign::Left;
    TextVerticalAlign m_CachedVerticalAlign = TextVerticalAlign::Top;
    BitmapFont* m_CachedFont = nullptr;
    int32_t m_CachedPixelScale = 1;

    // The rows of the bake that hold glyph pixels.
    int32_t m_CropFirstRow = 0;
    int32_t m_CropHeight = 0;
    float m_CropPivotY = 0.5f;
};

}  // namespace Deki2D
