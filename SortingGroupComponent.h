#pragma once

#include <cstdint>
#include "DekiComponent.h"
#include "reflection/DekiProperty.h"
#include "../rendering/ISortableProvider.h"

/**
 * @brief Component that groups children for sorting purposes
 *
 * Add this component to a parent object to make its children sort
 * together as a group. Children render relative to each other based
 * on their individual sortingOrder values.
 *
 * Usage:
 * @code
 * auto* group = parent->AddComponent<SortingGroupComponent>();
 * group->sorting_order = 10;  // Where this group appears in the scene
 * // Children with sortingOrder 0 render behind children with sortingOrder 5
 * @endcode
 *
 * The group's sorting_order determines where the entire group appears
 * relative to siblings. Children within the group are sorted among
 * themselves by their individual sortingOrder values.
 */
class SortingGroupComponent : public DekiComponent, public ISortableProvider
{
public:
    DEKI_COMPONENT(SortingGroupComponent, DekiComponent, "2D", "8d743475-7905-4cca-b12c-9c397162851d", "DEKI_FEATURE_SORTING_GROUP")

    /** @brief Sorting order for this group (affects where all children render in scene) */
    DEKI_EXPORT
    int32_t sorting_order = 0;

    SortingGroupComponent() = default;
    virtual ~SortingGroupComponent() = default;

    int32_t GetSortingOrder() const override { return sorting_order; }
    void SetSortingOrder(int32_t order) { sorting_order = order; }
};

// Generated property metadata (after class definition for offsetof)
#include "generated/SortingGroupComponent.gen.h"
