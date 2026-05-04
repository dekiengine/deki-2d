#include "SpriteComponent.h"
#include "DekiObject.h"
#include "deki-rendering/CameraComponent.h"
#include "profiling/DekiProfiler.h"
#include "DekiLogSystem.h"

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
        if (!spr || !spr->has_chroma_key)
            return;
        src.hasChromaKey = true;
        if (src.isRGB565)
        {
            src.keyR = (uint8_t)((spr->transparent_r >> 3) << 3);
            src.keyG = (uint8_t)((spr->transparent_g >> 2) << 2);
            src.keyB = (uint8_t)((spr->transparent_b >> 3) << 3);
        }
        else
        {
            src.keyR = spr->transparent_r;
            src.keyG = spr->transparent_g;
            src.keyB = spr->transparent_b;
        }
        if (attachRowSpans)
            src.chromaRowSpans = spr->chromaRowSpans;
    }
}

// ============================================================================
// Component Registration
// ============================================================================
// NOTE: s_Properties[] and s_ComponentMeta are now auto-generated in
// SpriteComponent.gen.h (included at end of SpriteComponent.h)


SpriteComponent::SpriteComponent(Sprite* spr)
: frame_x(0)
, frame_y(0)
, frame_width(0)  // 0 means use full sprite width
, frame_height(0) // 0 means use full sprite height
, tint_color(deki::Color::White)
, render_mode(SpriteRenderMode::Normal)
, width(0)
, height(0)
, m_cachedFrameBuffer(nullptr)
, m_cachedFrameSize(0)
{
    if (spr)
        sprite = spr;  // AssetRef assignment operator
}

void SpriteComponent::SetTint(const deki::Color& color)
{
    tint_color = color;
}

void SpriteComponent::SetTint(uint8_t r, uint8_t g, uint8_t b)
{
    tint_color = deki::Color(r, g, b);
}

void SpriteComponent::ClearTint()
{
    tint_color = deki::Color::White;
}

void SpriteComponent::SetFrameRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    frame_x = x;
    frame_y = y;
    frame_width = w;
    frame_height = h;
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
        frame_x = frame->x;
        frame_y = frame->y;
        frame_width = frame->width;
        frame_height = frame->height;
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

    // Free cached frame buffer
    delete[] m_cachedFrameBuffer;
    m_cachedFrameBuffer = nullptr;
    m_cachedFrameSize = 0;

    // Free cached Tiled/NineSlice bake buffer
    delete[] m_cachedRenderBuffer;
    m_cachedRenderBuffer = nullptr;
    m_cachedRenderSize   = 0;
    m_cachedRenderW      = 0;
    m_cachedRenderH      = 0;
    m_cachedRenderSrc    = nullptr;
    m_cachedRenderMode   = SpriteRenderMode::Normal;
}

// ============================================================
// UNIFIED RENDERING (QuadBlit)
// ============================================================

bool SpriteComponent::RenderContent(const DekiObject* owner,
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
    outTintR = tint_color.r;
    outTintG = tint_color.g;
    outTintB = tint_color.b;
    outTintA = tint_color.a;

    // Determine format properties
    bool isRGB565 = (spr->format == Texture2D::TextureFormat::RGB565 ||
                     spr->format == Texture2D::TextureFormat::RGB565A8);
    bool hasAlpha = spr->has_alpha;
    int32_t bytesPerPixel = Texture2D::GetBytesPerPixel(spr->format);

    // Tiled / NineSlice: stretch the whole sprite into a cached bake buffer.
    // These modes are mutually exclusive with animation frame extraction.
    // width/height of 0 means "use sprite native size" (matches the
    // frame_width=0 convention elsewhere in this component).
    if (render_mode == SpriteRenderMode::Tiled || render_mode == SpriteRenderMode::NineSlice)
    {
        int32_t target_w = (width  > 0) ? width  : spr->width;
        int32_t target_h = (height > 0) ? height : spr->height;

        // Hard guard: bake helpers assume positive dims and at least 1 source
        // pixel per axis. Refuse degenerate input rather than crash.
        if (target_w <= 0 || target_h <= 0 || spr->width <= 0 || spr->height <= 0)
        {
            DEKI_LOG_ERROR("SpriteComponent: invalid dims (target %dx%d, sprite %dx%d)",
                           target_w, target_h, spr->width, spr->height);
            return false;
        }

        if (render_mode == SpriteRenderMode::NineSlice)
        {
            if (!spr->has_nine_slice)
            {
                DEKI_LOG_ERROR("SpriteComponent: NineSlice mode but sprite has no 9-slice borders");
                return false;
            }
            int32_t min_w = spr->nine_slice_left + spr->nine_slice_right;
            int32_t min_h = spr->nine_slice_top  + spr->nine_slice_bottom;
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
            m_cachedRenderMode != render_mode)
        {
            size_t need = (size_t)target_w * (size_t)target_h * (size_t)bytesPerPixel;
            if (m_cachedRenderSize != need)
            {
                delete[] m_cachedRenderBuffer;
                m_cachedRenderBuffer = new uint8_t[need];
                m_cachedRenderSize   = need;
            }
            if (render_mode == SpriteRenderMode::NineSlice)
                Sprite::BakeNineSliceInto(m_cachedRenderBuffer, target_w, target_h, spr);
            else
                Sprite::BakeTiledInto(m_cachedRenderBuffer, target_w, target_h, spr);

            m_cachedRenderSrc  = spr;
            m_cachedRenderW    = target_w;
            m_cachedRenderH    = target_h;
            m_cachedRenderMode = render_mode;
        }

        outSource = QuadBlit::MakeSource(
            m_cachedRenderBuffer,
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
        outSource.pixelsPerMeter = spr->pixels_per_meter;
        outPivotX = spr->pivot_x;
        outPivotY = spr->pivot_y;
        return true;
    }

    // Use component frame settings, or sprite defaults if component doesn't specify
    int32_t effectiveFrameWidth = (frame_width > 0) ? frame_width : spr->default_frame_width;
    int32_t effectiveFrameHeight = (frame_height > 0) ? frame_height : spr->default_frame_height;

    // Check if we have a frame (either component-specified or sprite default)
    bool hasFrame = (frame_x != 0 || frame_y != 0 || effectiveFrameWidth > 0 || effectiveFrameHeight > 0);

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
            spr->alphaRowSpans
        );
        // Full-sprite blit uses the same row layout as the source — spans apply.
        ApplyChromaKey(outSource, spr, /*attachRowSpans=*/true);
        outSource.pixelsPerMeter = spr->pixels_per_meter;
        outPivotX = spr->pivot_x;
        outPivotY = spr->pivot_y;
    }
    else
    {
        // Animation frame - copy sub-region to new buffer
        int32_t fw = (effectiveFrameWidth > 0) ? effectiveFrameWidth : spr->width;
        int32_t fh = (effectiveFrameHeight > 0) ? effectiveFrameHeight : spr->height;

        // Clamp frame to sprite bounds
        if (frame_x + fw > spr->width) fw = spr->width - frame_x;
        if (frame_y + fh > spr->height) fh = spr->height - frame_y;
        if (fw <= 0 || fh <= 0)
            return false;

        // Reuse cached frame buffer to avoid per-frame allocation
        size_t frameSize = fw * fh * bytesPerPixel;
        if (m_cachedFrameSize != frameSize)
        {
            delete[] m_cachedFrameBuffer;
            m_cachedFrameBuffer = new uint8_t[frameSize];
            m_cachedFrameSize = frameSize;
        }

        // Copy rows from sprite to frame buffer
        for (int32_t y = 0; y < fh; y++)
        {
            const uint8_t* srcRow = spr->data + ((frame_y + y) * spr->width + frame_x) * bytesPerPixel;
            uint8_t* dstRow = m_cachedFrameBuffer + y * fw * bytesPerPixel;
            std::memcpy(dstRow, srcRow, fw * bytesPerPixel);
        }

        outSource = QuadBlit::MakeSource(
            m_cachedFrameBuffer,
            fw,
            fh,
            bytesPerPixel,
            hasAlpha,
            isRGB565,
            false  // ownsPixels = false - we manage this buffer's lifetime
        );
        // Frame is a sub-region of the source — its row layout doesn't match
        // the sprite's chromaRowSpans (different y origin and width). Use
        // per-pixel chroma compare without spans for animation frames.
        ApplyChromaKey(outSource, spr, /*attachRowSpans=*/false);
        outSource.pixelsPerMeter = spr->pixels_per_meter;
        // Frame uses center pivot
        outPivotX = 0.5f;
        outPivotY = 0.5f;
    }

    return true;
}

// Platform-specific helpers removed - rendering now done via QuadBlit
