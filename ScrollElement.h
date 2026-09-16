#pragma once

#include <cstdint>
#include <deki/Component.h>
#include <deki/reflection/Property.h>

namespace Deki2D
{

/**
 * @brief Defines the size of a scroll item for ScrollComponent
 *
 * Add this component to each child (or the Template child) of a ScrollComponent
 * to define the item's width and height. ScrollComponent reads these values
 * to compute item_size automatically.
 *
 * Supports visual resizing via the rect tool in the editor.
 */
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Declares one list item's size so its Scroll Component can lay the list out.")
DEKI_FORMER_NAME("ScrollElement")
class ScrollElement : public Deki::Component
{
public:

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of one item in meters. The scroll uses it to work out spacing and how far it can travel.")
    DEKI_UNIT(Distance)
    float width = 6.25f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of one item in meters.")
    DEKI_UNIT(Distance)
    float height = 3.75f;

    ScrollElement() = default;
    virtual ~ScrollElement() = default;
};

// Generated property metadata (after class definition for offsetof)

}  // namespace Deki2D
