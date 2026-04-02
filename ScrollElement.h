#pragma once

#include <cstdint>
#include "DekiComponent.h"
#include "reflection/DekiProperty.h"

/**
 * @brief Defines the size of a scroll item for ScrollComponent
 *
 * Add this component to each child (or the Template child) of a ScrollComponent
 * to define the item's width and height. ScrollComponent reads these values
 * to compute item_size automatically.
 *
 * Supports visual resizing via the rect tool in the editor.
 */
class ScrollElement : public DekiComponent
{
public:
    DEKI_COMPONENT(ScrollElement, DekiComponent, "2D", "dcc9b892-ec9b-4eae-90d5-561b5591860a", "DEKI_FEATURE_SCROLL")

    DEKI_EXPORT
    int32_t width = 100;

    DEKI_EXPORT
    int32_t height = 60;

    ScrollElement() = default;
    virtual ~ScrollElement() = default;
};

// Generated property metadata (after class definition for offsetof)
#include "generated/ScrollElement.gen.h"
