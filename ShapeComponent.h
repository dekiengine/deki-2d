#pragma once

#include <deki/providers/Buffer.h>

#include <stdint.h>

#include "deki-rendering/RendererComponent.h"
#include "ShapeMask.h"
#include <deki/Color.h>
#include <deki/reflection/Property.h>

namespace Deki2D
{

/// Draws a flat shape: a rectangle with rounded corners, an ellipse, an arc,
/// a triangle or a tick, filled or as an outline, with smooth edges.
///
/// The shape is baked once into an 8-bit coverage mask, a byte a pixel, and
/// drawn tinted with `color`, so the colour reaches the screen at the
/// display's own precision and a colour change costs no bake. The mask is
/// baked again only when the geometry changes, into memory it keeps: a shape
/// that grows and shrinks (a progress bar's fill) allocates only when it
/// outgrows what it had.
///
/// The UI widgets (SliderComponent, SwitchComponent and the rest) build
/// their looks from shapes on child objects, so each part can be restyled.
///
/// Example:
///
///     auto* shape = entity->AddComponent<ShapeComponent>();
///     shape->kind = ShapeKind::Rectangle;
///     shape->width = 6.0f;
///     shape->height = 2.0f;
///     shape->cornerRadius = 1.0f;
///     shape->color = Deki::Color(64, 128, 255);
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Draws a flat shape with smooth edges: rounded rectangle, ellipse, arc, triangle or tick.")
class ShapeComponent : public DekiRendering::RendererComponent
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("Which shape to draw. Custom draws what code writes into it.")
    ShapeKind kind = ShapeKind::Rectangle;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the shape in meters.")
    DEKI_UNIT(Distance)
    float width = 4.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the shape in meters.")
    DEKI_UNIT(Distance)
    float height = 4.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("The colour the shape is drawn in. Its alpha fades the whole shape.")
    Deki::Color color = Deki::Color(255, 255, 255);

    DEKI_VISIBLE_WHEN(kind, Rectangle)
    DEKI_EXPORT
    DEKI_TOOLTIP("Radius of the rounded corners in meters. Half the height makes a pill.")
    DEKI_UNIT(Distance)
    float cornerRadius = 0.0f;

    DEKI_VISIBLE_WHEN(kind, Rectangle, Ellipse, Arc, Triangle, Check)
    DEKI_EXPORT
    DEKI_TOOLTIP("0 fills the shape. Above 0, only a line this thick is drawn along the inside of the edge. An arc "
                 "and a tick are always lines; at 0 they pick a thickness from their size.")
    DEKI_UNIT(Distance)
    float lineWidth = 0.0f;

    DEKI_VISIBLE_WHEN(kind, Arc)
    DEKI_EXPORT
    DEKI_TOOLTIP("Where the arc starts. 0 points right, and the arc runs counter-clockwise from here.")
    DEKI_UNIT(Angle)
    float startAngle = 0.0f;

    DEKI_VISIBLE_WHEN(kind, Arc)
    DEKI_EXPORT
    DEKI_TOOLTIP("Where the arc ends. A full turn past the start draws a whole ring.")
    DEKI_UNIT(Angle)
    float endAngle = 6.2831853f;

    DEKI_VISIBLE_WHEN(kind, Arc)
    DEKI_EXPORT
    DEKI_TOOLTIP("Round the arc's ends instead of cutting them square.")
    bool roundCaps = false;

    DEKI_VISIBLE_WHEN(kind, Triangle)
    DEKI_EXPORT
    DEKI_TOOLTIP("Which way the triangle points.")
    ShapeDirection direction = ShapeDirection::Down;

    ShapeComponent() = default;
    ~ShapeComponent() override;

    /// Sets the size in meters.
    void SetSize(float newWidth, float newHeight);

    /// Sets the arc's ends in radians.
    void SetArc(float newStartAngle, float newEndAngle);

    /// For kind Custom: a cleared mask `w` x `h` pixels, row 0 at the top,
    /// `w` bytes a row, for code to write coverage into (255 solid, 0
    /// clear). The shape draws it across width x height. Call EndCustomMask
    /// when done. Returns nullptr when there is no room.
    uint8_t* BeginCustomMask(int32_t w, int32_t h);

    /// Publishes what was written since BeginCustomMask.
    void EndCustomMask();

    /// The parameters the bake would use at `pixelsPerMeter`.
    ShapeMaskParams MaskParams(float pixelsPerMeter) const;

    bool GetContentExtents(float& outWidth, float& outHeight) const override
    {
        if (width <= 0.0f || height <= 0.0f)
        {
            return false;
        }
        outWidth = width;
        outHeight = height;
        return true;
    }

    /// Changes whenever the mask is baked again.
    bool GetContentVersion(uint32_t& outVersion) const override
    {
        outVersion = m_BakeVersion;
        return true;
    }

    bool RenderContent(const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX, float& outPivotY,
                       uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB, uint8_t& outTintA) override;

private:
    // The coverage mask, a byte a pixel. It only grows: a smaller bake uses
    // the front of it, so a shape that changes size every frame does not
    // allocate every frame.
    Deki::Buffer<uint8_t> m_Mask;
    int32_t m_MaskW = 0;
    int32_t m_MaskH = 0;
    // The pixels per meter a shape was baked at, which it is drawn at too.
    // 0 for a custom mask, which covers the shape's size.
    float m_MaskPPM = 0.0f;
    uint32_t m_BakeVersion = 0;
    uint64_t m_BakeKey = 0;
    bool m_HasBake = false;

    // Makes room for a w x h mask without shrinking. False when there is none.
    bool Reserve(int32_t w, int32_t h);
    uint64_t ComputeBakeKey(int32_t w, int32_t h, float pixelsPerMeter) const;
};

}  // namespace Deki2D
