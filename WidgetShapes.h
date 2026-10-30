#pragma once

// Helpers for widget parts drawn by a ShapeComponent. Inline, so they come
// and go with the shape feature.

#include <deki/Color.h>
#include <deki/Object.h>
#include "ShapeComponent.h"
#include "WidgetParts.h"

namespace Deki2D
{
namespace WidgetParts
{

/// The part's ShapeComponent, or nullptr when it has none (or no part).
inline ShapeComponent* Shape(Deki::Object* part)
{
    return part ? part->GetComponent<ShapeComponent>() : nullptr;
}

/// The shape part `name`, made in the editor when missing and given `kind`
/// and `color` then. Later it keeps whatever it was restyled to.
inline Deki::Object* ShapePart(Deki::Object* owner, const char* name, ShapeKind kind, const Deki::Color& color,
                               float cornerRadius = 0.0f)
{
    bool created = false;
    Deki::Object* part = FindOrCreate<ShapeComponent>(owner, name, created);
    if (created)
    {
        if (ShapeComponent* shape = Shape(part))
        {
            shape->kind = kind;
            shape->color = color;
            shape->cornerRadius = cornerRadius;
        }
    }
    return part;
}

/// Sizes and places a shape part, when there is one.
inline void PlaceShape(Deki::Object* part, float x, float y, float width, float height)
{
    if (!part)
    {
        return;
    }
    part->SetLocalPosition(x, y);
    if (ShapeComponent* shape = Shape(part))
    {
        shape->SetSize(width, height);
    }
}

/// Recolours a shape part, when there is one.
inline void TintShape(Deki::Object* part, const Deki::Color& color)
{
    if (ShapeComponent* shape = Shape(part))
    {
        shape->color = color;
    }
}

}  // namespace WidgetParts
}  // namespace Deki2D
