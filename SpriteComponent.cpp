#include "SpriteComponent.h"
#include <deki/providers/Memory.h>
#include "PixelFormat.h"
#include <deki/Object.h>
#include <deki/Engine.h>
#include "deki-rendering/CameraComponent.h"
#include <deki/profiling/Profiler.h>
#include <deki/LogSystem.h>

#include <cstddef>
#include <algorithm>
#include <cmath>

namespace
{
    // Populate chroma-key fields on a QuadBlit::Source from a Sprite. Quantizes
    // the key to 5/6/5 precision when the source is RGB565-family so it matches
    // pixels extracted at that precision. attachRowSpans is false for sprites
    // whose pixel buffer is a derived bake (frame copy, tiled, 9-slice) — the
    // source-level row spans don't map to the derived buffer's rows.
    inline void ApplyChromaKey(QuadBlit::Source& src, const Sprite* spr, bool attachRowSpans)
    {
        if (!spr || !spr->hasChromaKey)
            return;
        src.hasChromaKey = true;
        if (src.isRGB565)
        {
            src.keyR = spr->transparentR;
            src.keyG = spr->transparentG;
            src.keyB = spr->transparentB;
            DekiPixel::QuantizeRGB565(src.keyR, src.keyG, src.keyB);
        }
        else
        {
            src.keyR = spr->transparentR;
            src.keyG = spr->transparentG;
            src.keyB = spr->transparentB;
        }
        if (attachRowSpans)
            src.chromaRowSpans = spr->chromaRowSpans.Data();
    }
}

// ============================================================================
// Component Registration
// ============================================================================
// NOTE: s_Properties[] and s_ComponentMeta are now auto-generated in
// SpriteComponent.gen.h (included at end of SpriteComponent.h)


SpriteComponent::SpriteComponent(Sprite* spr)
: frameX(0)
, frameY(0)
, frameWidth(0)  // 0 means use full sprite width
, frameHeight(0) // 0 means use full sprite height
, tintColor(Deki::Color::White)
, renderMode(SpriteRenderMode::Normal)
, width(0.0f)
, height(0.0f)
{
    if (spr)
        sprite = spr;  // AssetRef assignment operator
}

void SpriteComponent::SetTint(const Deki::Color& color)
{
    tintColor = color;
}

void SpriteComponent::SetTint(uint8_t r, uint8_t g, uint8_t b)
{
    tintColor = Deki::Color(r, g, b);
}

void SpriteComponent::ClearTint()
{
    tintColor = Deki::Color::White;
}

void SpriteComponent::SetFrameRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    frameX = x;
    frameY = y;
    frameWidth = w;
    frameHeight = h;
}

void SpriteComponent::OnAssetRefResolved(const char* propertyName, void* asset, const char* guid)
{
    // Check if this is the sprite property being resolved
    if (std::strcmp(propertyName, "sprite") != 0)
        return;

    Sprite* spr = static_cast<Sprite*>(asset);
    if (!spr)
        return;

    // Check if the GUID matches a frame in the sprite's frame list (SubAsset)
    // Sprites without frames (e.g. procedural assets) are normal - no warning needed
    const SpriteFrame* frame = spr->FindFrame(guid);
    if (frame)
    {
        frameX = frame->x;
        frameY = frame->y;
        frameWidth = frame->width;
        frameHeight = frame->height;
    }
}

// NOTE: Lifecycle methods (LoadAssets, UnloadAssets) are no longer needed
// because AssetRef<Sprite> auto-loads based on property metadata.
// HOWEVER: UnloadAssets() is still needed for editor asset hot-reloading.

void SpriteComponent::UnloadAssets()
{
    // Clear the cached sprite pointer and load flag to force reload on next ProcessInternalAwake()
    // This is essential for editor asset modification workflow:
    // 1. User modifies asset (e.g., ProceduralSprite properties)
    // 2. Editor calls InvalidateAllAssets() → UnloadAssets() on all components
    // 3. Cleared ptr + loadAttempted triggers full re-resolution on next frame
    // 4. AssetManager loads the fresh .dtex file with updated pixel data
    sprite.ptr = nullptr;
    sprite.loadAttempted = false;

    // Free cached Tiled/NineSlice bake buffer
    m_cachedRenderBuffer.Reset();
    m_cachedRenderW      = 0;
    m_cachedRenderH      = 0;
    m_cachedRenderSrc    = nullptr;
    m_cachedRenderMode   = SpriteRenderMode::Normal;
}

SpriteComponent::~SpriteComponent()
{
    // The bake buffer was only ever freed by UnloadAssets(), which the runtime
    // never calls, so every tiled / nine-slice sprite leaked it on scene unload.
    m_cachedRenderBuffer.Reset();
}

// ============================================================
// UNIFIED RENDERING (QuadBlit)
// ============================================================

bool SpriteComponent::GetContentExtents(float& outWidth, float& outHeight) const
{
    const Sprite* spr = sprite.Get();
    if (!spr || spr->width <= 0 || spr->height <= 0)
        return false;
    if (renderMode == SpriteRenderMode::Tiled || renderMode == SpriteRenderMode::NineSlice)
    {
        // The bake is width x height meters (0 = the sprite's native size).
        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        outWidth = (width > 0.0f) ? width : static_cast<float>(spr->width) / (ppm > 0.0f ? ppm : 1.0f);
        outHeight = (height > 0.0f) ? height : static_cast<float>(spr->height) / (ppm > 0.0f ? ppm : 1.0f);
        return true;
    }
    // Normal: the drawn region at the sprite's own pixels-per-meter. A frame
    // (component or sprite default) is smaller than the texture; the value
    // here mirrors RenderContent's frame choice, before its clamp to the
    // texture edge (which only makes the drawn frame smaller).
    const float ppm = (spr->pixelsPerMeter > 0.0f) ? spr->pixelsPerMeter : 1.0f;
    const int32_t fwEff = (frameWidth > 0) ? frameWidth : spr->defaultFrameWidth;
    const int32_t fhEff = (frameHeight > 0) ? frameHeight : spr->defaultFrameHeight;
    const bool hasFrame = (frameX != 0 || frameY != 0 || fwEff > 0 || fhEff > 0);
    const int32_t drawnW = (hasFrame && fwEff > 0) ? fwEff : spr->width;
    const int32_t drawnH = (hasFrame && fhEff > 0) ? fhEff : spr->height;
    outWidth = static_cast<float>(drawnW) / ppm;
    outHeight = static_cast<float>(drawnH) / ppm;
    return true;
}

void SpriteComponent::ApplyFlip(QuadBlit::Source& src) const
{
    src.flipH = flipHorizontal;
    src.flipV = flipVertical;
}

bool SpriteComponent::RenderContent(const Deki::Object* owner,
                                     QuadBlit::Source& outSource,
                                     float& outPivotX,
                                     float& outPivotY,
                                     uint8_t& outTintR,
                                     uint8_t& outTintG,
                                     uint8_t& outTintB,
                                     uint8_t& outTintA)
{
    DEKI_PROFILE_SCOPE_N("SpriteComponent::RenderContent");

    Sprite* spr = sprite.Get();
    if (!spr || !spr->data)
        return false;

    // Output tint color
    outTintR = tintColor.r;
    outTintG = tintColor.g;
    outTintB = tintColor.b;
    outTintA = tintColor.a;

    // Determine format properties
    bool isRGB565 = (spr->format == Texture2D::TextureFormat::RGB565 ||
                     spr->format == Texture2D::TextureFormat::RGB565A8);
    bool hasAlpha = spr->hasAlpha;
    int32_t bytesPerPixel = Texture2D::GetBytesPerPixel(spr->format);

    // Tiled / NineSlice: stretch the whole sprite into a cached bake buffer.
    // These modes are mutually exclusive with animation frame extraction.
    // width/height of 0 means "use sprite native size" (matches the
    // frameWidth=0 convention elsewhere in this component).
    if (renderMode == SpriteRenderMode::Tiled || renderMode == SpriteRenderMode::NineSlice)
    {
        // width/height are world meters; pixel-baking math runs in source pixels.
        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        int32_t target_w = (width  > 0.0f) ? static_cast<int32_t>(width  * ppm) : spr->width;
        int32_t target_h = (height > 0.0f) ? static_cast<int32_t>(height * ppm) : spr->height;

        // Hard guard: bake helpers assume positive dims and at least 1 source
        // pixel per axis. Refuse degenerate input rather than crash.
        if (target_w <= 0 || target_h <= 0 || spr->width <= 0 || spr->height <= 0)
        {
            DEKI_LOG_ERROR("SpriteComponent: invalid dims (target %dx%d, sprite %dx%d)",
                           target_w, target_h, spr->width, spr->height);
            return false;
        }

        if (renderMode == SpriteRenderMode::NineSlice)
        {
            if (!spr->hasNineSlice)
            {
                DEKI_LOG_ERROR("SpriteComponent: NineSlice mode but sprite has no 9-slice borders");
                return false;
            }
            int32_t min_w = spr->nineSliceLeft + spr->nineSliceRight;
            int32_t min_h = spr->nineSliceTop  + spr->nineSliceBottom;
            // Borders must also fit inside the SOURCE — otherwise BakeNineSliceInto
            // computes a negative center region and corner reads can underflow.
            if (min_w >= spr->width || min_h >= spr->height)
            {
                DEKI_LOG_ERROR("SpriteComponent: 9-slice borders %dx%d exceed source size %dx%d",
                               min_w, min_h, spr->width, spr->height);
                return false;
            }
            if (target_w < min_w || target_h < min_h)
            {
                DEKI_LOG_ERROR("SpriteComponent: 9-slice target %dx%d smaller than borders %dx%d",
                               target_w, target_h, min_w, min_h);
                return false;
            }
        }

        // (Re-)bake when source / size / mode changed
        if (m_cachedRenderSrc != spr ||
            m_cachedRenderW   != target_w ||
            m_cachedRenderH   != target_h ||
            m_cachedRenderMode != renderMode)
        {
            size_t need = (size_t)target_w * (size_t)target_h * (size_t)bytesPerPixel;
            // Allocate() leaves an unchanged size alone, so the reuse path
            // costs nothing. Sized by the object on screen, which a device can
            // refuse; not drawing it beats a reboot.
            if (!m_cachedRenderBuffer.Allocate(need, Deki::MemoryUse::Buffer,
                                               "SpriteComponent::bake"))
            {
                DEKI_LOG_WARNING("SpriteComponent: no room for a %dx%d bake (%u bytes); "
                                 "not drawing it",
                                 (int)target_w, (int)target_h, (unsigned)need);
                return false;
            }
            if (renderMode == SpriteRenderMode::NineSlice)
                Sprite::BakeNineSliceInto(m_cachedRenderBuffer.Data(), target_w, target_h, spr);
            else
                Sprite::BakeTiledInto(m_cachedRenderBuffer.Data(), target_w, target_h, spr);

            m_cachedRenderSrc  = spr;
            m_cachedRenderW    = target_w;
            m_cachedRenderH    = target_h;
            m_cachedRenderMode = renderMode;
        }

        outSource = QuadBlit::MakeSource(
            m_cachedRenderBuffer.Data(),
            target_w,
            target_h,
            bytesPerPixel,
            hasAlpha,
            isRGB565,
            false  // ownsPixels = false - component owns this buffer
        );
        // Tiled / 9-slice produce a derived buffer with different dimensions
        // from the source — sprite's chromaRowSpans don't apply.
        ApplyChromaKey(outSource, spr, /*attachRowSpans=*/false);
        outSource.pixelsPerMeter = spr->pixelsPerMeter;
        ApplyFlip(outSource);
        outPivotX = spr->pivotX;
        outPivotY = spr->pivotY;
        return true;
    }

    // Use component frame settings, or sprite defaults if component doesn't specify
    int32_t effectiveFrameWidth = (frameWidth > 0) ? frameWidth : spr->defaultFrameWidth;
    int32_t effectiveFrameHeight = (frameHeight > 0) ? frameHeight : spr->defaultFrameHeight;

    // Check if we have a frame (either component-specified or sprite default)
    bool hasFrame = (frameX != 0 || frameY != 0 || effectiveFrameWidth > 0 || effectiveFrameHeight > 0);

    if (!hasFrame)
    {
        // Full sprite - point directly to sprite's data
        outSource = QuadBlit::MakeSource(
            spr->data,
            spr->width,
            spr->height,
            bytesPerPixel,
            hasAlpha,
            isRGB565,
            false,  // ownsPixels = false - sprite owns its data
            spr->alphaRowSpans.Data()
        );
        // Full-sprite blit uses the same row layout as the source — spans apply.
        ApplyChromaKey(outSource, spr, /*attachRowSpans=*/true);
        outSource.pixelsPerMeter = spr->pixelsPerMeter;
        outPivotX = spr->pivotX;
        outPivotY = spr->pivotY;
    }
    else
    {
        // Animation frame - copy sub-region to new buffer
        int32_t fw = (effectiveFrameWidth > 0) ? effectiveFrameWidth : spr->width;
        int32_t fh = (effectiveFrameHeight > 0) ? effectiveFrameHeight : spr->height;

        // Clamp frame to sprite bounds
        if (frameX < 0 || frameY < 0 || frameX >= spr->width || frameY >= spr->height)
            return false;
        if (frameX + fw > spr->width) fw = spr->width - frameX;
        if (frameY + fh > spr->height) fh = spr->height - frameY;
        if (fw <= 0 || fh <= 0)
            return false;

        // Point QuadBlit at the frame inside the sprite's own pixels. The
        // stride keeps the atlas row pitch, so nothing is copied: this used to
        // memcpy the whole frame into a cached buffer on every render.
        outSource = QuadBlit::MakeSource(
            spr->data + ((size_t)frameY * spr->width + frameX) * bytesPerPixel,
            fw,
            fh,
            bytesPerPixel,
            hasAlpha,
            isRGB565,
            false  // ownsPixels = false - sprite owns its data
        );
        outSource.stride = spr->width * bytesPerPixel;
        // Frame is a sub-region of the source — its row layout doesn't match
        // the sprite's chromaRowSpans (different y origin and width). Use
        // per-pixel chroma compare without spans for animation frames.
        ApplyChromaKey(outSource, spr, /*attachRowSpans=*/false);
        outSource.pixelsPerMeter = spr->pixelsPerMeter;
        // Frame uses center pivot
        outPivotX = 0.5f;
        outPivotY = 0.5f;
    }

    ApplyFlip(outSource);
    return true;
}

// Platform-specific helpers removed - rendering now done via QuadBlit
