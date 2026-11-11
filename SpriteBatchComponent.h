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

/// Draws many pieces of one sprite, each with its own place, frame, flips,
/// tint and sorting order, as that many sprite objects would and with the
/// same pixels, for far less: a piece is read from this component, where an
/// object costs a walk through its own object, components and transform
/// every frame. For whatever a game places many of, such as soil, crops or
/// scenery. Each piece is sorted on its own, so a character can walk in
/// front of one and behind another.
///
/// Pieces are added from code and are not saved with the scene.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Draws many pieces of one sprite, each with its own place, frame and sorting order.")
class SpriteBatchComponent : public DekiRendering::RendererComponent
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The image every piece is cut from, whole or out of a spritesheet.")
    Deki::AssetRef<Sprite> sprite;

    SpriteBatchComponent() = default;
    ~SpriteBatchComponent() override = default;

    /// Adds a piece at (x, y) meters from the object, showing a rect of the
    /// sprite (its stored pixels; a width or height of 0 is the sprite's
    /// default frame, or all of it), in `sortingOrder`. Returns its index,
    /// which stays its own until Clear. -1 when there is no memory for it.
    int32_t Add(float x, float y, int32_t frameX, int32_t frameY, int32_t frameWidth, int32_t frameHeight,
                int32_t sortingOrder);

    /// The number of pieces, shown or not.
    uint32_t GetCount() const { return m_Count; }

    void SetPlace(uint32_t index, float x, float y, int32_t sortingOrder);
    void SetFrameRect(uint32_t index, int32_t x, int32_t y, int32_t width, int32_t height);
    /// A hidden piece draws nothing and keeps its index.
    void SetVisible(uint32_t index, bool visible);
    void SetFlip(uint32_t index, bool horizontal, bool vertical);
    /// White means no tint.
    void SetTint(uint32_t index, const Deki::Color& color);

    /// Removes every piece.
    void Clear() { m_Count = 0; }

    uint32_t GetPieceCount() const override { return m_Count; }
    void GetPiecePlace(uint32_t index, float& outX, float& outY, int32_t& outSortingOrder) const override;
    bool GetPieceExtents(uint32_t index, float& outWidth, float& outHeight) const override;
    bool RenderPieceContent(uint32_t index, const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX,
                            float& outPivotY, uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB,
                            uint8_t& outTintA) override;
    /// The sprite's pixels never change behind it while it is loaded.
    bool GetPieceVersion(uint32_t index, uint32_t& outVersion) const override
    {
        (void)index;
        outVersion = 0;
        return true;
    }
    bool GetPieceDrawKey(uint32_t index, uint64_t& outKey) const override;

    /// Drops the loaded sprite so it loads again; the editor calls it when an asset changes.
    void UnloadAssets() override
    {
        sprite.ptr = nullptr;
        sprite.loadAttempted = false;
    }

private:
    struct Piece
    {
        float x = 0.0f, y = 0.0f;
        int32_t sortingOrder = 0;
        int32_t frameX = 0, frameY = 0, frameWidth = 0, frameHeight = 0;
        Deki::Color tint = Deki::Color::White;
        bool visible = true;
        bool flipHorizontal = false;
        bool flipVertical = false;
    };
    // In external memory: a batch holds as many pieces as a game places.
    Deki::Buffer<Piece> m_Pieces;
    uint32_t m_Count = 0;

    Piece* PieceAt(uint32_t index) { return index < m_Count ? m_Pieces.Data() + index : nullptr; }
    const Piece* PieceAt(uint32_t index) const { return index < m_Count ? m_Pieces.Data() + index : nullptr; }
};

}  // namespace Deki2D
