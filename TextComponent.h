#pragma once

#include <deki/providers/Buffer.h>

#include <cstdint>
#include <string>
#include <vector>
#include "deki-rendering/RendererComponent.h"
#include "BitmapFont.h"
#include <deki/Color.h>
#include <deki/assets/AssetRef.h>

/**
 * @brief Text alignment options
 */
enum class TextAlign : uint8_t
{
    Left = 0,
    Center = 1,
    Right = 2
};

/**
 * @brief Text vertical alignment options
 *
 * Top/Middle/Bottom (0-2) are the legacy modes. The additional anchors use
 * font-wide typographic metrics so text centers optically regardless of
 * whether the string contains ascenders or descenders.
 */
enum class TextVerticalAlign : uint8_t
{
    Top        = 0,
    Middle     = 1,  // Legacy: centers on visual bounds of the whole font (ascent+descent)
    Bottom     = 2,
    CapCenter  = 3,  // Centers on cap-height — best for uppercase / mixed UI labels
    XCenter    = 4,  // Centers on x-height — best for lowercase-heavy body text
    TypoCenter = 5,  // Centers on typographic midline; same as Middle for single-line
    Baseline   = 6   // Baseline sits on the container center line
};

/**
 * @brief Component for rendering text using bitmap fonts
 *
 * TextComponent renders text strings using BitmapFont for glyph data.
 * Supports color tinting, alignment, and word wrapping.
 */
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Draws text with a bitmap font, alignment and word wrap.")
class TextComponent : public RendererComponent
{
public:

    TextComponent();
    virtual ~TextComponent();

    // ========================================================================
    // Editor-visible properties
    // ========================================================================

    /** @brief Text to display (editor-editable) */
    DEKI_EXPORT
    std::string text;

    /** @brief Font asset reference (GUID stored in editor, auto-loaded) */
    DEKI_EXPORT
    Deki::AssetRef<BitmapFont> font;

#ifdef DEKI_EDITOR
    /** @brief Font size in pixels (maps to baked variant) */
    DEKI_EXPORT
    DEKI_EDITOR_ONLY
    int32_t fontSize = 16;

    /** @brief Enable live font preview (editor-only, not serialized) */
    bool previewEnabled = false;

    /** @brief Font size to preview (editor-only, not serialized) */
    int32_t previewSize = 16;

    /** @brief True if fontSize is not available as a baked variant (editor-only) */
    bool fontSizeUnavailable = false;

    /**
     * @brief What the editor's font-resolve hook last synced font.guid from
     * (editor-only, not serialized). The hook derives the baked variant's GUID
     * from (font.source, fontSize) with a string build, a deterministic-GUID
     * hash and an asset-pipeline path lookup; with these it does that once per
     * change instead of once per frame per text.
     */
    std::string editorSyncedFontSource;
    int32_t editorSyncedFontSize = -1;
    std::string editorSyncedFontGuid;
#endif

    /**
     * @brief Set the text to display
     * @param text Text string (copied internally)
     */
    void SetText(const char* text);

    /**
     * @brief Set the text to display
     * @param text Text string
     */
    void SetText(const std::string& text);

    /**
     * @brief Get the current text
     * @return Current text string
     */
    const std::string& GetText() const { return text; }

    /**
     * @brief Font resolve callback for editor integration
     *
     * Called during RenderContent to let external code (editor) handle font resolution
     * (GUID sync, preview, baking). Return non-null to use that font directly,
     * or nullptr to fall through to the runtime path (font.Get()).
     */
    using FontResolveCallback = BitmapFont*(*)(TextComponent*);
    static void SetFontResolveCallback(FontResolveCallback cb);

    /**
     * @brief Set the font to use for rendering
     * @param f Pointer to bitmap font (not owned by TextComponent)
     */
    void SetFont(BitmapFont* f);

    /**
     * @brief Get the current font
     * @return Current font or nullptr
     */
    BitmapFont* GetFont() { return font.Get(); }
    const BitmapFont* GetFont() const { return font.Get(); }

    /**
     * @brief Set text color
     * @param color Text color
     */
    void SetColor(const Deki::Color& color);

    /**
     * @brief Set text color (RGB convenience)
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void SetColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Get text color
     * @return Current text color
     */
    const Deki::Color& GetColor() const { return color; }

    /**
     * @brief Set horizontal text alignment
     * @param alignVal Alignment mode
     */
    void SetAlign(TextAlign alignVal) { align = alignVal; }

    /**
     * @brief Get horizontal text alignment
     * @return Current alignment
     */
    TextAlign GetAlign() const { return align; }

    /**
     * @brief Set vertical text alignment
     * @param alignVal Vertical alignment mode
     */
    void SetVerticalAlign(TextVerticalAlign alignVal) { verticalAlign = alignVal; }

    /**
     * @brief Get vertical text alignment
     * @return Current vertical alignment
     */
    TextVerticalAlign GetVerticalAlign() const { return verticalAlign; }

    /**
     * @brief Set text box width
     * @param w Width in meters
     */
    void SetWidth(float w) { width = w; }

    /**
     * @brief Get text box width
     * @return Width in meters
     */
    float GetWidth() const { return width; }

    /**
     * @brief Set text box height
     * @param h Height in meters
     */
    void SetHeight(float h) { height = h; }

    /**
     * @brief Get text box height
     * @return Height in meters
     */
    float GetHeight() const { return height; }

    /**
     * @brief Get the measured width of the current text
     * @return Width in pixels
     */
    int32_t GetTextWidth() const;

    /**
     * @brief Get the measured height of the current text
     * @return Height in pixels (considering line wrapping)
     */
    int32_t GetTextHeight() const;

    // Clear cached font pointer to force reload (used when fonts are re-baked in editor)
    void UnloadAssets() override;

    // Unified rendering via QuadBlit
    // Culling extents (meters): the box the content is baked into.
    bool GetContentExtents(float& outWidth, float& outHeight) const override
    {
        if (width <= 0.0f || height <= 0.0f)
            return false;
        outWidth = width;
        outHeight = height;
        return true;
    }

    bool RenderContent(const Deki::Object* owner,
                       QuadBlit::Source& outSource,
                       float& outPivotX,
                       float& outPivotY,
                       uint8_t& outTintR,
                       uint8_t& outTintG,
                       uint8_t& outTintB,
                       uint8_t& outTintA) override;

    // ========================================================================
    // Layout methods (shared between runtime and editor)
    // ========================================================================

    /**
     * @brief Glyph layout info for rendering
     */
    struct GlyphLayout
    {
        const GlyphInfo* glyph;  // Glyph data from font
        float worldX;            // X position relative to component center
        float worldY;            // Y position relative to component center
    };

    /**
     * @brief Calculate glyph positions for rendering
     * @param fontPtr Font to use (can be different from component's font for editor)
     * @param outGlyphs Output vector of glyph layouts
     *
     * Positions are in WORLD coordinates relative to component center.
     * Editor multiplies by zoom, runtime uses directly.
     */
    void CalculateGlyphLayout(const BitmapFont* fontPtr, std::vector<GlyphLayout>& outGlyphs) const;

    /**
     * @brief Word-wrap text to fit within maxWidth (public for editor use)
     * @param fontPtr Font to use for measurements
     * @return Vector of wrapped lines
     */
    std::vector<std::string> WrapTextWithFont(const BitmapFont* fontPtr) const;

    // ========================================================================
    // Editor-visible properties (public for reflection)
    // ========================================================================

    /** @brief Text box width in meters */
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float width = 6.25f;

    /** @brief Text box height in meters */
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float height = 1.5f;

    /** @brief Text color */
    DEKI_EXPORT
    Deki::Color color;

    /**
     * @brief Decoration color (outline / shadow).
     * Only used when the bound font is v4+ and was baked with a decoration other
     * than None. Ignored for plain alpha fonts.
     */
    DEKI_EXPORT
    Deki::Color decorationColor;

    /** @brief Pixel scale for bitmap fonts (1x, 2x, 3x nearest-neighbor) */
    DEKI_EXPORT
    int32_t pixelScale = 1;

    /** @brief Horizontal text alignment */
    DEKI_EXPORT
    TextAlign align = TextAlign::Left;

    /** @brief Vertical text alignment (new components default to cap-center for optical centering) */
    DEKI_EXPORT
    TextVerticalAlign verticalAlign = TextVerticalAlign::CapCenter;

    // Invalidate the render cache (call when text/font/color/size changes)
    void InvalidateRenderCache();

private:
    static FontResolveCallback s_fontResolveCallback;

    // Cached render buffer
    // Owning, and it knows its own size.
    Deki::Buffer<uint8_t> m_cachedBuffer;
    std::string m_cachedText;
    int32_t m_cachedWidth = 0;
    int32_t m_cachedHeight = 0;
    Deki::Color m_cachedColor;
    Deki::Color m_cachedDecorationColor;
    TextAlign m_cachedAlign = TextAlign::Left;
    TextVerticalAlign m_cachedVerticalAlign = TextVerticalAlign::Top;
    BitmapFont* m_cachedFont = nullptr;
    int32_t m_cachedPixelScale = 1;

    // Cached vertical crop bounds (tight Y range of actual glyph content)
    int32_t m_cropFirstRow = 0;
    int32_t m_cropHeight = 0;
    float m_cropPivotY = 0.5f;

};

// Generated property metadata (after class definition for offsetof)
