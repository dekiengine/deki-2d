#pragma once

#include <deki/providers/Buffer.h>

#include <stdint.h>

#include "deki-rendering/RendererComponent.h"
#include "Sprite.h"
#include <deki/Color.h>
#include <deki/assets/AssetRef.h>
#include <deki/reflection/Property.h>

/**
 * @brief How a SpriteComponent draws its sprite.
 *
 * Normal    - Single quad at the sprite's native size (default).
 * Tiled     - Repeat the sprite to fill render_width x render_height.
 * NineSlice - 9-slice scale to render_width x render_height. Requires the
 *             sprite to have hasNineSlice = true with valid borders.
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
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Draws a sprite, with tint, flip, and tiled or 9-slice modes.")
class SpriteComponent : public RendererComponent
{
public:

    // Sprite asset - uses AssetRef for automatic GUID storage and loading
    DEKI_EXPORT
    Deki::AssetRef<Sprite> sprite;

    // Frame rectangle (for sprite atlas/sub-texture rendering)
    // Controlled by AnimationComponent - NOT exported
    int32_t frameX;
    int32_t frameY;
    int32_t frameWidth;
    int32_t frameHeight;

    // Tint color (white = no tint)
    DEKI_EXPORT
    Deki::Color tintColor;

    // Render mode: single quad / tiled / 9-slice
    DEKI_EXPORT
    SpriteRenderMode renderMode = SpriteRenderMode::Normal;

    /** @brief Mirror left to right. */
    DEKI_EXPORT
    bool flipHorizontal = false;

    /** @brief Mirror top to bottom. */
    DEKI_EXPORT
    bool flipVertical = false;

    // Rendered size in meters, used by Tiled and NineSlice modes (0 = sprite
    // native size). The sprite is baked into a pixel buffer sized by
    // round(width * ppm) so the on-screen quad expands while the transform's
    // scale stays the same.
    DEKI_VISIBLE_WHEN(renderMode, Tiled, NineSlice)
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float width;

    DEKI_VISIBLE_WHEN(renderMode, Tiled, NineSlice)
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float height;

    SpriteComponent(Sprite* spr = nullptr);

    // Culling extents (meters): the sprite's native size, or the tiled/nine-slice box.
    bool GetContentExtents(float& outWidth, float& outHeight) const override;

    /**
     * @brief Set the tint color for the sprite
     * @param color Tint color (white = no tint)
     */
    void SetTint(const Deki::Color& color);

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
    bool RenderContent(const Deki::Object* owner,
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

    ~SpriteComponent() override;

private:
    // Copy the flip flags onto the blit source. QuadBlit mirrors while it
    // samples, so no buffer is produced and no pivot moves; a flipped blit
    // takes its generic path, which is why the row-span fast paths stay
    // correct.
    void ApplyFlip(QuadBlit::Source& src) const;

    // Cached pre-baked buffer for Tiled / NineSlice modes (re-baked on size/source/mode change).
    // Animation frames need no buffer: they point QuadBlit at the sub-rect of
    // the sprite's own pixels via Source::stride.
    // Owning, and it knows its own size.
    Deki::Buffer<uint8_t> m_cachedRenderBuffer;
    int32_t          m_cachedRenderW      = 0;
    int32_t          m_cachedRenderH      = 0;
    const Sprite*    m_cachedRenderSrc    = nullptr;
    SpriteRenderMode m_cachedRenderMode   = SpriteRenderMode::Normal;
};

// Generated property metadata (after class definition for offsetof)
