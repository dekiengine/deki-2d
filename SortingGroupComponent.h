#pragma once

#include <cstdint>
#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include <deki/ISortableProvider.h>

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
 * group->sortingOrder = 10;  // Where this group appears in the scene
 * // Children with sortingOrder 0 render behind children with sortingOrder 5
 * @endcode
 *
 * The group's sortingOrder determines where the entire group appears
 * relative to siblings. Children within the group are sorted among
 * themselves by their individual sortingOrder values.
 */
class SortingGroupComponent : public Deki::Component, public Deki::ISortableProvider
{
public:
    DEKI_COMPONENT(SortingGroupComponent, Deki::Component, "2D", "8d743475-7905-4cca-b12c-9c397162851d", "DEKI_FEATURE_SORTING_GROUP")
    DEKI_DESCRIPTION("Makes its children sort together as one unit against the rest of the scene.")

    /** @brief Sorting order for this group (affects where all children render in scene) */
    DEKI_EXPORT
    int32_t sortingOrder = 0;

    SortingGroupComponent() = default;
    virtual ~SortingGroupComponent() = default;

    int32_t GetSortingOrder() const override { return sortingOrder; }
    void SetSortingOrder(int32_t order) { sortingOrder = order; }
};

// Generated property metadata (after class definition for offsetof)
#include "generated/SortingGroupComponent.gen.h"
