#pragma once

#include <cstdint>
#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include <deki/IClipProvider.h>
#include <deki/ISortableProvider.h>

/**
 * @brief Component that clips children to a rectangular region
 *
 * Add this component to any object to clip its children's rendering
 * to the specified width/height bounds. The clip region is centered
 * on the object's position.
 *
 * Usage:
 * @code
 * auto* clip = entity->AddComponent<ClipComponent>();
 * clip->width = 100;
 * clip->height = 50;
 * // All children will be clipped to this 100x50 region
 * @endcode
 *
 * ClipComponents can be nested - child clips are intersected with parent clips.
 */
class ClipComponent : public Deki::Component, public Deki::IClipProvider, public Deki::ISortableProvider
{
public:
    DEKI_COMPONENT(ClipComponent, Deki::Component, "Core", "063a42d4-bfe1-49b9-b620-0cbf87ad720f", "DEKI_FEATURE_CLIP")
    DEKI_DESCRIPTION("Clips its children's rendering to a rectangle.")

    /** @brief Width of clip region in meters */
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float width = 6.25f;

    /** @brief Height of clip region in meters */
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float height = 6.25f;

    /** @brief Sorting order for this clip region (lower = behind) */
    DEKI_EXPORT
    int32_t sortingOrder = 0;

    ClipComponent() = default;
    virtual ~ClipComponent() = default;

    // ISortableProvider
    int32_t GetSortingOrder() const override { return sortingOrder; }

    // IClipProvider
    float GetClipWidth() const override { return width; }
    float GetClipHeight() const override { return height; }
};

// Generated property metadata (after class definition for offsetof)
#include "generated/ClipComponent.gen.h"
