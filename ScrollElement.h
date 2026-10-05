#pragma once

#include <cstdint>
#include <deki/Component.h>
#include <deki/reflection/Property.h>

namespace Deki2D
{

/// The size of one ScrollComponent item. Put it on each item child (or on
/// the template) so the scroll can lay the list out. The editor's rect tool
/// can resize it.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Declares one list item's size so its Scroll Component can lay the list out.")
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

}  // namespace Deki2D
