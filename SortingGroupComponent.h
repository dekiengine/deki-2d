#pragma once

#include <cstdint>
#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include <deki/ISortableProvider.h>

namespace Deki2D
{

/// Makes a parent's children sort together as one group. The group's
/// sortingOrder places the whole group among its siblings; inside it, the
/// children sort among themselves by their own sortingOrder.
///
///     auto* group = parent->AddComponent<SortingGroupComponent>();
///     group->sortingOrder = 10;  // Where this group appears in the scene
///     // Children with sortingOrder 0 render behind children with sortingOrder 5
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Makes its children sort together as one unit against the rest of the scene.")
class SortingGroupComponent : public Deki::Component, public Deki::ISortableProvider
{
public:
    /// Where the whole group draws in the scene.
    DEKI_EXPORT
    DEKI_TOOLTIP(
        "Draw order for this object's whole subtree, treated as one unit. Children keep their order relative to each "
        "other but the group moves together, which stops one child sorting between another group's children.")
    int32_t sortingOrder = 0;

    SortingGroupComponent() = default;
    virtual ~SortingGroupComponent() = default;

    int32_t GetSortingOrder() const override { return sortingOrder; }
    void SetSortingOrder(int32_t order) { sortingOrder = order; }
};

}  // namespace Deki2D
