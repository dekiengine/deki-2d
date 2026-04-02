#pragma once

#include <cstdint>
#include "DekiComponent.h"
#include "reflection/DekiProperty.h"
#include "../rendering/IClipProvider.h"
#include "../rendering/ISortableProvider.h"

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
class ClipComponent : public DekiComponent, public IClipProvider, public ISortableProvider
{
public:
    DEKI_COMPONENT(ClipComponent, DekiComponent, "Core", "063a42d4-bfe1-49b9-b620-0cbf87ad720f", "DEKI_FEATURE_CLIP")

    /** @brief Width of clip region in pixels */
    DEKI_EXPORT
    int32_t width = 100;

    /** @brief Height of clip region in pixels */
    DEKI_EXPORT
    int32_t height = 100;

    /** @brief Sorting order for this clip region (lower = behind) */
    DEKI_EXPORT
    int32_t sorting_order = 0;

    ClipComponent() = default;
    virtual ~ClipComponent() = default;

    // ISortableProvider
    int32_t GetSortingOrder() const override { return sorting_order; }

    // IClipProvider
    float GetClipWidth() const override { return static_cast<float>(width); }
    float GetClipHeight() const override { return static_cast<float>(height); }

    /**
     * @brief Get clip bounds in screen coordinates
     * @param screenX Object's screen X position (center)
     * @param screenY Object's screen Y position (center)
     * @param outLeft Left edge of clip region
     * @param outTop Top edge of clip region
     * @param outRight Right edge of clip region
     * @param outBottom Bottom edge of clip region
     */
    void GetClipBounds(int32_t screenX, int32_t screenY,
                       int32_t& outLeft, int32_t& outTop,
                       int32_t& outRight, int32_t& outBottom) const
    {
        outLeft = screenX - width / 2;
        outTop = screenY - height / 2;
        outRight = outLeft + width;
        outBottom = outTop + height;
    }
};

// Generated property metadata (after class definition for offsetof)
#include "generated/ClipComponent.gen.h"
