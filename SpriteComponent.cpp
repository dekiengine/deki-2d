#include "SpriteComponent.h"
#include <deki/providers/Memory.h>
#include "PixelFormat.h"
#include <deki/Object.h>
#include <deki/Engine.h>
#include "deki-rendering/CameraComponent.h"
#include <deki/profiling/Profiler.h>
#include <deki/LogSystem.h>
#include <deki/assets/AssetManager.h>

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace Deki2D
{

namespace
{
// Tints an RGB565 or RGB565A8 bake in place, with the blit's arithmetic
// (Div255 of the 8-bit channel by the tint), so the blit can draw it untinted:
// a tinted blit runs per pixel, an untinted one copies its opaque runs.
void TintBake(uint8_t* data, size_t pixels, int32_t bytesPerPixel, uint8_t tintR, uint8_t tintG, uint8_t tintB)
{
    uint8_t r[32], g[64], b[32];
    for (uint32_t i = 0; i < 32; i++)
    {
        r[i] = static_cast<uint8_t>(DekiPixel::Div255((i << 3) * tintR) >> 3);
        b[i] = static_cast<uint8_t>(DekiPixel::Div255((i << 3) * tintB) >> 3);
    }
    for (uint32_t i = 0; i < 64; i++)
    {
        g[i] = static_cast<uint8_t>(DekiPixel::Div255((i << 2) * tintG) >> 2);
    }
    for (size_t i = 0; i < pixels; i++, data += bytesPerPixel)
    {
        const uint32_t v = DekiPixel::Load565(data);
        const uint32_t t = (static_cast<uint32_t>(r[v >> 11]) << 11) | (static_cast<uint32_t>(g[(v >> 5) & 0x3F]) << 5) |
                           b[v & 0x1F];
        DekiPixel::Store565(data, static_cast<uint16_t>(t));
    }
}

// Each row's run of fully opaque pixels in an RGB565A8 bake: the first one and
// the run from it, as QuadBlit's alphaRowSpans. A row with none gets [0, 0).
void FindOpaqueRuns(const uint8_t* data, int32_t width, int32_t height, int16_t* spans)
{
    for (int32_t y = 0; y < height; y++)
    {
        const uint8_t* row = data + static_cast<size_t>(y) * width * 3;
        int32_t start = 0;
        while (start < width && row[start * 3 + 2] != 255)
        {
            start++;
        }
        int32_t end = start;
        while (end < width && row[end * 3 + 2] == 255)
        {
            end++;
        }
        if (start >= width)
        {
            start = end = 0;
        }
        spans[y * 2] = static_cast<int16_t>(start);
        spans[y * 2 + 1] = static_cast<int16_t>(end);
    }
}

// Each row's longest run of fully transparent pixels in an RGB565A8 bake, as
// QuadBlit's clearRowSpans. A row with none gets [0, 0).
void FindClearRuns(const uint8_t* data, int32_t width, int32_t height, int16_t* spans)
{
    for (int32_t y = 0; y < height; y++)
    {
        const uint8_t* row = data + static_cast<size_t>(y) * width * 3;
        int32_t bestStart = 0, bestEnd = 0;
        for (int32_t x = 0; x < width;)
        {
            if (row[x * 3 + 2] != 0)
            {
                x++;
                continue;
            }
            const int32_t start = x;
            while (x < width && row[x * 3 + 2] == 0)
            {
                x++;
            }
            if (x - start > bestEnd - bestStart)
            {
                bestStart = start;
                bestEnd = x;
            }
        }
        spans[y * 2] = static_cast<int16_t>(bestStart);
        spans[y * 2 + 1] = static_cast<int16_t>(bestEnd);
    }
}

// Fills the chroma-key fields of a QuadBlit::Source from a Sprite. For
// RGB565-family sources the key is quantized to 5/6/5 so it matches the
// pixels. attachRowSpans is false when the blit is not the whole sprite (a
// frame, a tiled or 9-slice bake): the sprite's row spans do not match its rows.
inline void ApplyChromaKey(QuadBlit::Source& src, const Sprite* spr, bool attachRowSpans)
{
    if (!spr || !spr->hasChromaKey)
    {
        return;
    }
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
    {
        src.chromaRowSpans = spr->chromaRowSpans.Data();
    }
}
}  // namespace

// ============================================================================
// Component Registration
// ============================================================================
// s_Properties[] and s_ComponentMeta are generated into SpriteComponent.gen.h.

SpriteComponent::SpriteComponent(Sprite* spr)
    : frameX(0),
      frameY(0),
      frameWidth(0)  // 0 means use full sprite width
      ,
      frameHeight(0)  // 0 means use full sprite height
      ,
      tintColor(Deki::Color::White),
      renderMode(SpriteRenderMode::Normal),
      width(0.0f),
      height(0.0f)
{
    if (spr)
    {
        sprite = spr;  // AssetRef assignment operator
    }
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
    m_FrameGuid[0] = '\0';  // a bare rect: nothing to look up again
}

void SpriteComponent::SetFrame(const SpriteFrame& frame)
{
    frameX = frame.x;
    frameY = frame.y;
    frameWidth = frame.width;
    frameHeight = frame.height;
    std::memcpy(m_FrameGuid, frame.guid, sizeof(m_FrameGuid));
    m_FrameGuid[sizeof(m_FrameGuid) - 1] = '\0';
    m_FrameEpoch = Deki::AssetManager::Get() ? Deki::AssetManager::Get()->GetEpoch() : 0;
}

void SpriteComponent::RefreshFrame(const Sprite* spr)
{
    if (m_FrameGuid[0] == '\0' || !spr)
    {
        return;
    }
    const uint64_t epoch = Deki::AssetManager::Get() ? Deki::AssetManager::Get()->GetEpoch() : 0;
    if (epoch == m_FrameEpoch)
    {
        return;
    }
    m_FrameEpoch = epoch;
    if (const SpriteFrame* frame = spr->FindFrame(m_FrameGuid))
    {
        frameX = frame->x;
        frameY = frame->y;
        frameWidth = frame->width;
        frameHeight = frame->height;
    }
}

void SpriteComponent::OnAssetRefResolved(const char* propertyName, void* asset, const char* guid)
{
    if (std::strcmp(propertyName, "sprite") != 0)
    {
        return;
    }

    Sprite* spr = static_cast<Sprite*>(asset);
    if (!spr)
    {
        return;
    }

    // A GUID that names one of the sprite's frames (a sub-asset) shows that
    // frame. Many sprites have no frames; that is not an error.
    const SpriteFrame* frame = spr->FindFrame(guid);
    if (frame)
    {
        SetFrame(*frame);
    }
}

// AssetRef<Sprite> loads the sprite by itself. UnloadAssets() is there for
// the editor's asset hot reload.

void SpriteComponent::UnloadAssets()
{
    // Clearing the pointer and the load flag makes the next
    // ProcessInternalAwake() load the sprite again. The editor relies on it:
    // 1. User modifies asset (e.g., ProceduralSprite properties)
    // 2. Editor calls InvalidateAllAssets(), so UnloadAssets() on all components
    // 3. The cleared pointer and flag resolve the reference again next frame
    // 4. AssetManager loads the fresh .dtex file with updated pixel data
    sprite.ptr = nullptr;
    sprite.loadAttempted = false;

    m_CachedRenderBuffer.Reset();
    m_CachedRenderW = 0;
    m_CachedRenderH = 0;
    m_CachedRenderSrc = nullptr;
    m_CachedRenderMode = SpriteRenderMode::Normal;
}

SpriteComponent::~SpriteComponent()
{
    // Must be freed here: the runtime never calls UnloadAssets().
    m_CachedRenderBuffer.Reset();
}

// ============================================================
// UNIFIED RENDERING (QuadBlit)
// ============================================================

bool SpriteComponent::GetContentExtents(float& outWidth, float& outHeight) const
{
    const Sprite* spr = sprite.Get();
    if (!spr || spr->width <= 0 || spr->height <= 0)
    {
        return false;
    }
    if (renderMode == SpriteRenderMode::Tiled || renderMode == SpriteRenderMode::NineSlice)
    {
        // The bake is width x height meters (0 = the frame's or sprite's
        // native size), in the sprite's stored pixels (fewer when Max Size
        // shrinks it).
        bool hasBorders = false;
        const Sprite::SliceRegion region = SliceSource(spr, hasBorders);
        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter * spr->sourceScale;
        outWidth = (width > 0.0f) ? width : static_cast<float>(region.width) / (ppm > 0.0f ? ppm : 1.0f);
        outHeight = (height > 0.0f) ? height : static_cast<float>(region.height) / (ppm > 0.0f ? ppm : 1.0f);
        return true;
    }
    // Normal: the drawn region at the sprite's own pixels-per-meter. A frame
    // (the component's or the sprite's default) is smaller than the texture.
    // This follows RenderContent's frame choice, before its clamp to the
    // texture edge, which only makes the drawn frame smaller.
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

Sprite::SliceRegion SpriteComponent::SliceSource(const Sprite* spr, bool& hasBorders) const
{
    Sprite::SliceRegion region;
    region.width = spr->width;
    region.height = spr->height;
    hasBorders = spr->hasNineSlice;
    region.left = spr->nineSliceLeft;
    region.right = spr->nineSliceRight;
    region.top = spr->nineSliceTop;
    region.bottom = spr->nineSliceBottom;

    // A shown frame, clamped to the texture.
    if (frameWidth <= 0 || frameHeight <= 0)
    {
        return region;
    }
    const int32_t x0 = std::clamp(frameX, int32_t{ 0 }, spr->width - 1);
    const int32_t y0 = std::clamp(frameY, int32_t{ 0 }, spr->height - 1);
    region.x = x0;
    region.y = y0;
    region.width = std::min(frameWidth, spr->width - x0);
    region.height = std::min(frameHeight, spr->height - y0);
    const SpriteFrame* frame = m_FrameGuid[0] ? spr->FindFrame(m_FrameGuid) : nullptr;
    hasBorders = frame && frame->hasNineSlice;
    region.left = hasBorders ? frame->nineSliceLeft : 0;
    region.right = hasBorders ? frame->nineSliceRight : 0;
    region.top = hasBorders ? frame->nineSliceTop : 0;
    region.bottom = hasBorders ? frame->nineSliceBottom : 0;
    return region;
}

void SpriteComponent::ApplyFlip(QuadBlit::Source& src) const
{
    src.flipH = flipHorizontal;
    src.flipV = flipVertical;
}

bool SpriteComponent::RenderContent(const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX,
                                    float& outPivotY, uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB,
                                    uint8_t& outTintA)
{
    DEKI_PROFILE_SCOPE_N("SpriteComponent::RenderContent");

    Sprite* spr = sprite.Get();
    if (!spr || !spr->data)
    {
        return false;
    }
    RefreshFrame(spr);

    outTintR = tintColor.r;
    outTintG = tintColor.g;
    outTintB = tintColor.b;
    outTintA = tintColor.a;

    // For sizing the bake buffer and striding into the atlas. The blit's own
    // layout comes from PixelLayout::FromTexture below.
    const int32_t bytesPerPixel = Deki::Texture2D::GetBytesPerPixel(spr->format);

    // Tiled / NineSlice: bake the shown frame, or the whole sprite, into the
    // cached buffer. A width or height of 0 means the native size, as
    // frameWidth=0 does elsewhere in this component.
    if (renderMode == SpriteRenderMode::Tiled || renderMode == SpriteRenderMode::NineSlice)
    {
        // width/height are world meters; the bake works in the sprite's
        // stored pixels (fewer when Max Size shrinks it).
        if (spr->width <= 0 || spr->height <= 0)
        {
            return false;
        }
        // The shown frame, or the whole sprite.
        bool hasBorders = false;
        const Sprite::SliceRegion region = SliceSource(spr, hasBorders);
        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter * spr->sourceScale;
        int32_t targetW = (width > 0.0f) ? static_cast<int32_t>(width * ppm) : region.width;
        int32_t targetH = (height > 0.0f) ? static_cast<int32_t>(height * ppm) : region.height;

        // The bake helpers need positive sizes and at least one source pixel
        // per axis. Refuse anything else rather than crash.
        if (targetW <= 0 || targetH <= 0 || region.width <= 0 || region.height <= 0)
        {
            DEKI_LOG_ERROR("SpriteComponent: invalid dims (target %dx%d, source %dx%d)", targetW, targetH, region.width,
                           region.height);
            return false;
        }

        if (renderMode == SpriteRenderMode::NineSlice)
        {
            if (!hasBorders)
            {
                // Once per sprite: the bake is retried every frame, which
                // would log about 180 times a second.
                if (!m_WarnedNoNineSlice)
                {
                    m_WarnedNoNineSlice = true;
                    DEKI_LOG_WARNING("SpriteComponent: '%s' is in 9-slice mode but its sprite has no 9-slice borders",
                                     owner ? owner->GetName().c_str() : "?");
                }
                return false;
            }
            int32_t minW = region.left + region.right;
            int32_t minH = region.top + region.bottom;
            // The borders must also fit inside the SOURCE, or the bake gets a
            // negative centre and the corner reads can underflow.
            if (minW >= region.width || minH >= region.height)
            {
                DEKI_LOG_ERROR("SpriteComponent: 9-slice borders %dx%d exceed source size %dx%d", minW, minH,
                               region.width, region.height);
                return false;
            }
            if (targetW < minW || targetH < minH)
            {
                DEKI_LOG_ERROR("SpriteComponent: 9-slice target %dx%d smaller than borders %dx%d", targetW, targetH,
                               minW, minH);
                return false;
            }
        }

        // The tint goes into the bake for the RGB565 formats; a chroma key is
        // compared against the untinted colour, so a keyed sprite keeps it for
        // the blit.
        const bool tintInBake = (spr->format == Deki::Texture2D::TextureFormat::RGB565 ||
                                 spr->format == Deki::Texture2D::TextureFormat::RGB565A8) &&
                                !spr->hasChromaKey;
        const Deki::Color bakeTint =
            tintInBake ? Deki::Color(tintColor.r, tintColor.g, tintColor.b, 255) : Deki::Color(255, 255, 255, 255);
        const bool tintChanged = bakeTint.r != m_CachedRenderTint.r || bakeTint.g != m_CachedRenderTint.g ||
                                 bakeTint.b != m_CachedRenderTint.b;

        // Bake again when the source, size, mode, region or baked tint changed.
        const Sprite::SliceRegion& was = m_CachedRenderRegion;
        const bool regionChanged = was.x != region.x || was.y != region.y || was.width != region.width ||
                                   was.height != region.height || was.left != region.left ||
                                   was.right != region.right || was.top != region.top || was.bottom != region.bottom;
        if (m_CachedRenderSrc != spr || m_CachedRenderW != targetW || m_CachedRenderH != targetH ||
            m_CachedRenderMode != renderMode || regionChanged || tintChanged)
        {
            size_t need = (size_t)targetW * (size_t)targetH * (size_t)bytesPerPixel;
            // Allocate() leaves an unchanged size alone, so reuse costs
            // nothing. The size follows the object on screen, which a device
            // may not have room for; skipping the draw beats a reboot.
            if (!m_CachedRenderBuffer.Allocate(need, Deki::Memory::External))
            {
                DEKI_LOG_WARNING("SpriteComponent: no room for a %dx%d bake (%u bytes); "
                                 "not drawing it",
                                 (int)targetW, (int)targetH, (unsigned)need);
                return false;
            }
            if (renderMode == SpriteRenderMode::NineSlice)
            {
                Sprite::BakeNineSliceRegion(m_CachedRenderBuffer.Data(), targetW, targetH, spr, region);
            }
            else
            {
                Sprite::BakeTiledRegion(m_CachedRenderBuffer.Data(), targetW, targetH, spr, region);
            }

            if (bakeTint.r != 255 || bakeTint.g != 255 || bakeTint.b != 255)
            {
                TintBake(m_CachedRenderBuffer.Data(), (size_t)targetW * (size_t)targetH, bytesPerPixel, bakeTint.r,
                         bakeTint.g, bakeTint.b);
            }
            const bool spans = spr->format == Deki::Texture2D::TextureFormat::RGB565A8 && spr->hasAlpha;
            if (spans && m_CachedRowSpans.Allocate((size_t)targetH * 2, Deki::Memory::External))
            {
                FindOpaqueRuns(m_CachedRenderBuffer.Data(), targetW, targetH, m_CachedRowSpans.Data());
            }
            else
            {
                m_CachedRowSpans.Reset();
            }
            if (spans && m_CachedClearSpans.Allocate((size_t)targetH * 2, Deki::Memory::External))
            {
                FindClearRuns(m_CachedRenderBuffer.Data(), targetW, targetH, m_CachedClearSpans.Data());
            }
            else
            {
                m_CachedClearSpans.Reset();
            }

            m_CachedRenderTint = bakeTint;
            m_CachedRenderRegion = region;
            m_CachedRenderSrc = spr;
            m_CachedRenderW = targetW;
            m_CachedRenderH = targetH;
            m_CachedRenderMode = renderMode;
        }

        outSource = QuadBlit::MakeSource(m_CachedRenderBuffer.Data(), targetW, targetH,
                                         QuadBlit::PixelLayout::FromTexture(spr->format, spr->hasAlpha),
                                         false,  // ownsPixels = false - component owns this buffer
                                         m_CachedRowSpans ? m_CachedRowSpans.Data() : nullptr);
        outSource.clearRowSpans = m_CachedClearSpans ? m_CachedClearSpans.Data() : nullptr;
        if (tintInBake)
        {
            // Already in the pixels; the alpha tint stays the blit's.
            outTintR = 255;
            outTintG = 255;
            outTintB = 255;
        }
        // The bake has other dimensions than the sprite, so its chromaRowSpans do not apply.
        ApplyChromaKey(outSource, spr, /*attachRowSpans=*/false);
        outSource.pixelsPerMeter = spr->pixelsPerMeter;
        ApplyFlip(outSource);
        outPivotX = spr->pivotX;
        outPivotY = spr->pivotY;
        return true;
    }

    // The component's frame, or the sprite's default frame when it sets none.
    int32_t effectiveFrameWidth = (frameWidth > 0) ? frameWidth : spr->defaultFrameWidth;
    int32_t effectiveFrameHeight = (frameHeight > 0) ? frameHeight : spr->defaultFrameHeight;

    bool hasFrame = (frameX != 0 || frameY != 0 || effectiveFrameWidth > 0 || effectiveFrameHeight > 0);

    if (!hasFrame)
    {
        // The whole sprite: point straight at its pixels.
        outSource = QuadBlit::MakeSource(spr->data, spr->width, spr->height,
                                         QuadBlit::PixelLayout::FromTexture(spr->format, spr->hasAlpha),
                                         false,  // ownsPixels = false - sprite owns its data
                                         spr->alphaRowSpans.Data());
        // Same rows as the sprite, so its spans apply.
        ApplyChromaKey(outSource, spr, /*attachRowSpans=*/true);
        outSource.pixelsPerMeter = spr->pixelsPerMeter;
        outPivotX = spr->pivotX;
        outPivotY = spr->pivotY;
    }
    else
    {
        // An animation frame.
        int32_t fw = (effectiveFrameWidth > 0) ? effectiveFrameWidth : spr->width;
        int32_t fh = (effectiveFrameHeight > 0) ? effectiveFrameHeight : spr->height;

        // Clamp the frame to the sprite.
        if (frameX < 0 || frameY < 0 || frameX >= spr->width || frameY >= spr->height)
        {
            return false;
        }
        if (frameX + fw > spr->width)
        {
            fw = spr->width - frameX;
        }
        if (frameY + fh > spr->height)
        {
            fh = spr->height - frameY;
        }
        if (fw <= 0 || fh <= 0)
        {
            return false;
        }

        // Point QuadBlit at the frame inside the sprite's own pixels. The
        // stride keeps the atlas row pitch, so nothing is copied.
        outSource = QuadBlit::MakeSource(spr->data + ((size_t)frameY * spr->width + frameX) * bytesPerPixel, fw, fh,
                                         QuadBlit::PixelLayout::FromTexture(spr->format, spr->hasAlpha),
                                         false  // ownsPixels = false - sprite owns its data
        );
        outSource.stride = spr->width * bytesPerPixel;
        // The frame's rows do not match the sprite's chromaRowSpans (other y
        // origin and width), so frames use the per-pixel chroma compare.
        ApplyChromaKey(outSource, spr, /*attachRowSpans=*/false);
        outSource.pixelsPerMeter = spr->pixelsPerMeter;
        // Frames pivot on their centre.
        outPivotX = 0.5f;
        outPivotY = 0.5f;
    }

    ApplyFlip(outSource);
    return true;
}

}  // namespace Deki2D
