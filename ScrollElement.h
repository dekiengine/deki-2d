#pragma once

#include <cstdint>
#include <deki/Component.h>
#include <deki/reflection/Property.h>

/**
 * @brief Defines the size of a scroll item for ScrollComponent
 *
 * Add this component to each child (or the Template child) of a ScrollComponent
 * to define the item's width and height. ScrollComponent reads these values
 * to compute item_size automatically.
 *
 * Supports visual resizing via the rect tool in the editor.
 */
class ScrollElement : public Deki::Component
{
public:
    DEKI_COMPONENT(ScrollElement, Deki::Component, "2D", "dcc9b892-ec9b-4eae-90d5-561b5591860a", "DEKI_FEATURE_SCROLL")
    DEKI_DESCRIPTION("Declares one list item's size so its Scroll Component can lay the list out.")

    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float width = 6.25f;

    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float height = 3.75f;

    ScrollElement() = default;
    virtual ~ScrollElement() = default;
};

// Generated property metadata (after class definition for offsetof)
#include "generated/ScrollElement.gen.h"
