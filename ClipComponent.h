#pragma once

#include <cstdint>
#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include <deki/IClipProvider.h>
#include <deki/ISortableProvider.h>

namespace Deki2D
{

/// Clips the rendering of an object's children to a width x height rectangle
/// centred on the object's position.
///
///     auto* clip = entity->AddComponent<ClipComponent>();
///     clip->width = 100;
///     clip->height = 50;  // children are now cut to this 100x50 region
///
/// Clips can be nested: a child clip is intersected with its parent's.
DEKI_CATEGORY("Core")
DEKI_DESCRIPTION("Clips its children's rendering to a rectangle.")
class ClipComponent : public Deki::Component, public Deki::IClipProvider, public Deki::ISortableProvider
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the clipping window in meters. Children are cut off at this edge.")
    DEKI_UNIT(Distance)
    float width = 6.25f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the clipping window in meters.")
    DEKI_UNIT(Distance)
    float height = 6.25f;

    // Lower is further behind
    DEKI_EXPORT
    DEKI_TOOLTIP("Draw order of the clipped group as a whole against everything outside it.")
    int32_t sortingOrder = 0;

    ClipComponent() = default;
    virtual ~ClipComponent() = default;

    // ISortableProvider
    int32_t GetSortingOrder() const override { return sortingOrder; }

    // IClipProvider
    float GetClipWidth() const override { return width; }
    float GetClipHeight() const override { return height; }
};

}  // namespace Deki2D
