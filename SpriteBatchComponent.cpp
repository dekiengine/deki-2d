#include "SpriteBatchComponent.h"
#include "SpriteComponent.h"
#include "deki-rendering/Fingerprint.h"
#include <deki/providers/Memory.h>

#include <cstring>
#include <new>

namespace Deki2D
{

// s_Properties[] and s_ComponentMeta are generated into SpriteBatchComponent.gen.h.

int32_t SpriteBatchComponent::Add(float x, float y, int32_t frameX, int32_t frameY, int32_t frameWidth,
                                  int32_t frameHeight, int32_t sortingOrder)
{
    if (m_Count == m_Pieces.Count())
    {
        // Grown by doubling; pieces are plain values, moved as they are.
        Deki::Buffer<Piece> grown;
        const size_t capacity = m_Pieces.Count() ? m_Pieces.Count() * 2 : 16;
        if (!grown.Allocate(capacity, Deki::Memory::External))
        {
            return -1;
        }
        if (m_Count > 0)
        {
            std::memcpy(static_cast<void*>(grown.Data()), m_Pieces.Data(), sizeof(Piece) * m_Count);
        }
        m_Pieces = std::move(grown);
    }
    Piece* p = new (m_Pieces.Data() + m_Count) Piece();
    p->x = x;
    p->y = y;
    p->sortingOrder = sortingOrder;
    p->frameX = frameX;
    p->frameY = frameY;
    p->frameWidth = frameWidth;
    p->frameHeight = frameHeight;
    return static_cast<int32_t>(m_Count++);
}

void SpriteBatchComponent::SetPlace(uint32_t index, float x, float y, int32_t sortingOrder)
{
    if (Piece* p = PieceAt(index))
    {
        p->x = x;
        p->y = y;
        p->sortingOrder = sortingOrder;
    }
}

void SpriteBatchComponent::SetFrameRect(uint32_t index, int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (Piece* p = PieceAt(index))
    {
        p->frameX = x;
        p->frameY = y;
        p->frameWidth = width;
        p->frameHeight = height;
    }
}

void SpriteBatchComponent::SetVisible(uint32_t index, bool visible)
{
    if (Piece* p = PieceAt(index))
    {
        p->visible = visible;
    }
}

void SpriteBatchComponent::SetFlip(uint32_t index, bool horizontal, bool vertical)
{
    if (Piece* p = PieceAt(index))
    {
        p->flipHorizontal = horizontal;
        p->flipVertical = vertical;
    }
}

void SpriteBatchComponent::SetTint(uint32_t index, const Deki::Color& color)
{
    if (Piece* p = PieceAt(index))
    {
        p->tint = color;
    }
}

void SpriteBatchComponent::GetPiecePlace(uint32_t index, float& outX, float& outY, int32_t& outSortingOrder) const
{
    const Piece* p = PieceAt(index);
    outX = p ? p->x : 0.0f;
    outY = p ? p->y : 0.0f;
    outSortingOrder = p ? p->sortingOrder : sortingOrder;
}

bool SpriteBatchComponent::GetPieceExtents(uint32_t index, float& outWidth, float& outHeight) const
{
    const Piece* p = PieceAt(index);
    const Sprite* spr = sprite.Get();
    if (!p || !spr || spr->width <= 0 || spr->height <= 0)
    {
        return false;
    }
    return SpriteComponent::FrameExtents(spr, p->frameX, p->frameY, p->frameWidth, p->frameHeight, outWidth,
                                         outHeight);
}

bool SpriteBatchComponent::RenderPieceContent(uint32_t index, const Deki::Object* owner, QuadBlit::Source& outSource,
                                              float& outPivotX, float& outPivotY, uint8_t& outTintR,
                                              uint8_t& outTintG, uint8_t& outTintB, uint8_t& outTintA)
{
    (void)owner;
    const Piece* p = PieceAt(index);
    Sprite* spr = sprite.Get();
    if (!p || !p->visible || !spr || !spr->data)
    {
        return false;
    }
    // As a SpriteComponent showing this rect in Normal mode draws it.
    outTintR = p->tint.r;
    outTintG = p->tint.g;
    outTintB = p->tint.b;
    outTintA = p->tint.a;
    if (!SpriteComponent::FrameSource(spr, p->frameX, p->frameY, p->frameWidth, p->frameHeight, outSource, outPivotX,
                                      outPivotY))
    {
        return false;
    }
    outSource.flipH = p->flipHorizontal;
    outSource.flipV = p->flipVertical;
    return true;
}

bool SpriteBatchComponent::GetPieceDrawKey(uint32_t index, uint64_t& outKey) const
{
    // The sprite as last resolved: the renderer measures in full whenever the
    // asset epoch moves, which is when it would resolve to another.
    const Piece* p = PieceAt(index);
    const Sprite* spr = static_cast<const Sprite*>(sprite.ptr);
    if (!p || !spr || !spr->data)
    {
        return false;
    }
    DekiRendering::Fingerprint f;
    f.Add(static_cast<const void*>(spr));
    f.Add(static_cast<const void*>(spr->data));
    f.Add(spr->Revision());
    f.Add(spr->width);
    f.Add(spr->height);
    f.Add(spr->pivotX);
    f.Add(spr->pivotY);
    f.Add(spr->pixelsPerMeter);
    f.Add(spr->defaultFrameWidth);
    f.Add(spr->defaultFrameHeight);
    f.Add(p->frameX);
    f.Add(p->frameY);
    f.Add(p->frameWidth);
    f.Add(p->frameHeight);
    f.Add(static_cast<uint32_t>(p->tint.r) | (static_cast<uint32_t>(p->tint.g) << 8) |
          (static_cast<uint32_t>(p->tint.b) << 16) | (static_cast<uint32_t>(p->tint.a) << 24));
    f.Add(static_cast<uint32_t>(p->visible) | (static_cast<uint32_t>(p->flipHorizontal) << 1) |
          (static_cast<uint32_t>(p->flipVertical) << 2));
    outKey = f.Value();
    return true;
}

}  // namespace Deki2D
