#include "SortingGroupComponent.h"
#include "../rendering/ComponentInterfaceAdapters.h"
#include "../rendering/ISortableProvider.h"

static struct SortingGroupInterfaceRegistrar {
    SortingGroupInterfaceRegistrar() {
        ComponentInterfaceAdapters::Register(
            ISortableProvider::InterfaceID,
            SortingGroupComponent::StaticType,
            [](DekiComponent* c) -> void* {
                return static_cast<ISortableProvider*>(static_cast<SortingGroupComponent*>(c));
            });
    }
} s_sortingGroupInterfaceRegistrar;
