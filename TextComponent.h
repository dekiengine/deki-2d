#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../rendering/RendererComponent.h"
#include "BitmapFont.h"
#include "Color.h"
#include "../../assets/AssetRef.h"

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
 */
enum class TextVerticalAlign : uint8_t
{
    Top = 0,
    Middle = 1,
    Bottom = 2
};

/**
 * @brief Component for rendering text using bitmap fonts
 *
 * TextComponent renders text strings using BitmapFont for glyph data.
 * Supports color tinting, alignment, and word wrapping.
 */
class TextComponent : public RendererComponent
{
public:
    DEKI_COMPONENT(TextComponent, RendererComponent, "2D", "5447ea24-d11f-4161-ae10-2f11e0a18d09", "DEKI_FEATURE_TEXT")

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
    int32_t fontSize = 16;

    /** @brief Enable live font preview (editor-only, not serialized) */
    bool previewEnabled = false;

    /** @brief Font size to preview (editor-only, not serialized) */
    int32_t previewSize = 16;

    /** @brief True if fontSize is not available as a baked variant (editor-only) */
    bool fontSizeUnavailable = false;
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
    void SetColor(const deki::Color& color);

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
    const deki::Color& GetColor() const { return color; }

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
     * @param w Width in pixels
     */
    void SetWidth(int32_t w) { width = w; }

    /**
     * @brief Get text box width
     * @return Width in pixels
     */
    int32_t GetWidth() const { return width; }

    /**
     * @brief Set text box height
     * @param h Height in pixels
     */
    void SetHeight(int32_t h) { height = h; }

    /**
     * @brief Get text box height
     * @return Height in pixels
     */
    int32_t GetHeight() const { return height; }

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
    bool RenderContent(const DekiObject* owner,
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

    /** @brief Text box width in pixels */
    DEKI_EXPORT
    int32_t width = 100;

    /** @brief Text box height in pixels */
    DEKI_EXPORT
    int32_t height = 24;

    /** @brief Text color */
    DEKI_EXPORT
    deki::Color color;

    /** @brief Horizontal text alignment */
    DEKI_EXPORT
    TextAlign align = TextAlign::Left;

    /** @brief Vertical text alignment */
    DEKI_EXPORT
    TextVerticalAlign verticalAlign = TextVerticalAlign::Top;

    // Invalidate the render cache (call when text/font/color/size changes)
    void InvalidateRenderCache();

private:
    // Cached render buffer
    uint8_t* m_cachedBuffer = nullptr;
    size_t m_cachedBufferSize = 0;
    std::string m_cachedText;
    int32_t m_cachedWidth = 0;
    int32_t m_cachedHeight = 0;
    deki::Color m_cachedColor;
    TextAlign m_cachedAlign = TextAlign::Left;
    TextVerticalAlign m_cachedVerticalAlign = TextVerticalAlign::Top;
    BitmapFont* m_cachedFont = nullptr;

    // Render a single glyph to the buffer
    void RenderGlyph(const GlyphInfo* glyph,
                     int32_t x,
                     int32_t y,
                     uint8_t* render_buffer,
                     int screen_width,
                     int screen_height);

    // Helper to measure width of a string
    int32_t MeasureLineWidth(const char* str, size_t len) const;

    // Word-wrap text to fit within maxWidth
    std::vector<std::string> WrapText(int32_t maxWidth) const;
};

// Generated property metadata (after class definition for offsetof)
#include "generated/TextComponent.gen.h"
