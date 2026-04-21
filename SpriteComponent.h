#pragma once

#include <stdint.h>

#include "deki-rendering/RendererComponent.h"
#include "Sprite.h"
#include "Color.h"
#include "assets/AssetRef.h"
#include "reflection/DekiProperty.h"

/**
 * @brief How a SpriteComponent draws its sprite.
 *
 * Normal    - Single quad at the sprite's native size (default).
 * Tiled     - Repeat the sprite to fill render_width x render_height.
 * NineSlice - 9-slice scale to render_width x render_height. Requires the
 *             sprite to have has_nine_slice = true with valid borders.
 */
enum class SpriteRenderMode : uint8_t
{
    Normal    = 0,
    Tiled     = 1,
    NineSlice = 2,
};

/**
 * @brief Sprite component for entities that have visual representation
 */
class SpriteComponent : public RendererComponent
{
public:
    DEKI_COMPONENT(SpriteComponent, RendererComponent, "2D", "ca17ba76-f46d-484d-97ff-6e8e5497ef8d", "DEKI_FEATURE_SPRITE")

    // Sprite asset - uses AssetRef for automatic GUID storage and loading
    DEKI_EXPORT
    Deki::AssetRef<Sprite> sprite;

    // Frame rectangle (for sprite atlas/sub-texture rendering)
    // Controlled by AnimationComponent - NOT exported
    int32_t frame_x;
    int32_t frame_y;
    int32_t frame_width;
    int32_t frame_height;

    // Tint color (white = no tint)
    DEKI_EXPORT
    deki::Color tint_color;

    // Render mode: single quad / tiled / 9-slice
    DEKI_EXPORT
    SpriteRenderMode render_mode = SpriteRenderMode::Normal;

    // Rendered size used by Tiled and NineSlice modes (0 = sprite native size).
    // The sprite is baked into a buffer of these dimensions, so the on-screen
    // quad expands accordingly while the transform's scale stays the same.
    DEKI_VISIBLE_WHEN(render_mode, Tiled, NineSlice)
    DEKI_EXPORT
    int32_t width;

    DEKI_VISIBLE_WHEN(render_mode, Tiled, NineSlice)
    DEKI_EXPORT
    int32_t height;

    SpriteComponent(Sprite* spr = nullptr);

    /**
     * @brief Set the tint color for the sprite
     * @param color Tint color (white = no tint)
     */
    void SetTint(const deki::Color& color);

    /**
     * @brief Set the tint color for the sprite
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     */
    void SetTint(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Clear tint (set to white)
     */
    void ClearTint();

    /**
     * @brief Set the animation frame rectangle for sub-texture rendering
     * @param x X offset within sprite texture
     * @param y Y offset within sprite texture
     * @param w Width of the frame (or 0 to use full sprite width)
     * @param h Height of the frame (or 0 to use full sprite height)
     */
    void SetFrameRect(int32_t x, int32_t y, int32_t w, int32_t h);

    // Unified rendering - returns sprite data for QuadBlit
    bool RenderContent(const DekiObject* owner,
                       QuadBlit::Source& outSource,
                       float& outPivotX,
                       float& outPivotY,
                       uint8_t& outTintR,
                       uint8_t& outTintG,
                       uint8_t& outTintB,
                       uint8_t& outTintA) override;

    // Called after sprite AssetRef is resolved - handles SubAsset frame lookup
    void OnAssetRefResolved(const char* propertyName, void* asset, const char* guid) override;

    // Clear cached sprite pointer to force reload (used when assets are modified in editor)
    void UnloadAssets() override;

private:
    // Cached frame buffer to avoid per-frame allocation for spritesheet frames
    uint8_t* m_cachedFrameBuffer = nullptr;
    size_t m_cachedFrameSize = 0;

    // Cached pre-baked buffer for Tiled / NineSlice modes (re-baked on size/source/mode change)
    uint8_t*         m_cachedRenderBuffer = nullptr;
    size_t           m_cachedRenderSize   = 0;
    int32_t          m_cachedRenderW      = 0;
    int32_t          m_cachedRenderH      = 0;
    const Sprite*    m_cachedRenderSrc    = nullptr;
    SpriteRenderMode m_cachedRenderMode   = SpriteRenderMode::Normal;
};

// Generated property metadata (after class definition for offsetof)
#include "generated/SpriteComponent.gen.h"
