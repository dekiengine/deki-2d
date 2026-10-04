#pragma once

#include <deki/providers/Buffer.h>

#include <stdint.h>

#include "deki-rendering/RendererComponent.h"
#include "Sprite.h"
#include <deki/Color.h>
#include <deki/assets/AssetRef.h>
#include <deki/reflection/Property.h>

namespace Deki2D
{

/// How a SpriteComponent draws its sprite.
///
/// Normal    - Single quad at the sprite's native size (default).
/// Tiled     - Repeat the sprite to fill width x height.
/// NineSlice - 9-slice scale to width x height. The sprite (or the shown
///             frame) needs valid 9-slice borders.
enum class SpriteRenderMode : uint8_t
{
    Normal = 0,
    Tiled = 1,
    NineSlice = 2,
};

/// Draws a sprite, with tint, flip, and tiled or 9-slice modes.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Draws a sprite, with tint, flip, and tiled or 9-slice modes.")
DEKI_FORMER_NAME("SpriteComponent")
class SpriteComponent : public DekiRendering::RendererComponent
{
public:
    // The AssetRef stores the GUID and loads the sprite.
    DEKI_EXPORT
    DEKI_TOOLTIP("The image to draw. Sprites come from a texture asset, either whole or cut out of a spritesheet.")
    Deki::AssetRef<Sprite> sprite;

    // The frame rectangle within the sprite, set by AnimationComponent. Not exported.
    int32_t frameX;
    int32_t frameY;
    int32_t frameWidth;
    int32_t frameHeight;

    // White = no tint.
    DEKI_EXPORT
    DEKI_TOOLTIP(
        "Multiplied into every pixel. White leaves the image alone; darker tints shade it, and the alpha fades it out.")
    Deki::Color tintColor;

    DEKI_EXPORT
    DEKI_TOOLTIP("Normal draws the sprite once. Tiled repeats it to fill the size below. Nine-slice stretches the "
                 "middle and leaves the corners intact, which is what you want for panels and buttons.")
    SpriteRenderMode renderMode = SpriteRenderMode::Normal;

    DEKI_EXPORT
    DEKI_TOOLTIP(
        "Mirror left to right. Cheaper than a second sprite and the usual way to face a character the other way.")
    bool flipHorizontal = false;

    DEKI_EXPORT
    DEKI_TOOLTIP("Mirror top to bottom.")
    bool flipVertical = false;

    // Drawn size in meters in Tiled and NineSlice modes (0 = the sprite's
    // native size). The sprite is baked into a pixel buffer of width * ppm
    // pixels, so the quad grows while the transform's scale stays the same.
    DEKI_VISIBLE_WHEN(renderMode, Tiled, NineSlice)
    DEKI_EXPORT
    DEKI_TOOLTIP("Drawn size in meters. Left at 0 the sprite's own pixel size is used, converted through the project's "
                 "pixels-per-meter.")
    DEKI_UNIT(Distance)
    float width;

    DEKI_VISIBLE_WHEN(renderMode, Tiled, NineSlice)
    DEKI_EXPORT
    DEKI_TOOLTIP("Drawn size in meters. Left at 0 the sprite's own pixel size is used, converted through the project's "
                 "pixels-per-meter.")
    DEKI_UNIT(Distance)
    float height;

    SpriteComponent(Sprite* spr = nullptr);

    // Culling extents (meters): the sprite's native size, or the tiled/nine-slice box.
    bool GetContentExtents(float& outWidth, float& outHeight) const override;

    /// White means no tint.
    void SetTint(const Deki::Color& color);

    void SetTint(uint8_t r, uint8_t g, uint8_t b);

    /// Sets the tint back to white.
    void ClearTint();

    /// Draws only this rectangle of the sprite, in its pixels. A width or
    /// height of 0 uses the sprite's full width or height.
    void SetFrameRect(int32_t x, int32_t y, int32_t w, int32_t h);

    /// Shows one of the sprite's frames. Unlike SetFrameRect, the frame is
    /// remembered by its GUID, so when the sprite is reloaded (a reimport:
    /// new frames, a different Max Size) its rect is looked up again instead
    /// of staying in the old texture's pixels.
    void SetFrame(const SpriteFrame& frame);

    // Hands QuadBlit the sprite's pixels.
    bool RenderContent(const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX, float& outPivotY,
                       uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB, uint8_t& outTintA) override;

    // When the sprite reference points at a frame (a sub-asset), shows that frame.
    void OnAssetRefResolved(const char* propertyName, void* asset, const char* guid) override;

    // Drops the cached sprite so it loads again; the editor calls it when an asset changes.
    void UnloadAssets() override;

    ~SpriteComponent() override;

private:
    // Copies the flip flags onto the blit source. QuadBlit mirrors while it
    // samples, so no buffer is made and no pivot moves. A flipped blit takes
    // the generic path, so the row-span fast paths stay correct.
    void ApplyFlip(QuadBlit::Source& src) const;

    // The baked pixels for Tiled and NineSlice modes, baked again when the
    // size, source or mode changes. Owning, and it knows its own size.
    // Animation frames need no buffer: they point QuadBlit at the sub-rect of
    // the sprite's own pixels via Source::stride.
    Deki::Buffer<uint8_t> m_CachedRenderBuffer;
    int32_t m_CachedRenderW = 0;
    int32_t m_CachedRenderH = 0;
    // The frame SetFrame showed, by GUID ("" after SetFrameRect), and the
    // asset epoch its rect was read at. RenderContent reads the rect again
    // when the epoch moves.
    char m_FrameGuid[37] = {};
    uint64_t m_FrameEpoch = 0;
    void RefreshFrame(const Sprite* spr);

    const Sprite* m_CachedRenderSrc = nullptr;
    SpriteRenderMode m_CachedRenderMode = SpriteRenderMode::Normal;
    Sprite::SliceRegion m_CachedRenderRegion;

    // What Tiled and NineSlice stretch: the shown frame, or the whole sprite
    // when none is, with that frame's (or the sprite's) 9-slice borders.
    // `hasBorders` is false when it has none.
    Sprite::SliceRegion SliceSource(const Sprite* spr, bool& hasBorders) const;
    // Set once the "9-slice mode without borders" warning is logged for this sprite.
    mutable bool m_WarnedNoNineSlice = false;
};

}  // namespace Deki2D
