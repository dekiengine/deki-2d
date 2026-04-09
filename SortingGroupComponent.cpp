#include "SortingGroupComponent.h"
#include "ComponentInterfaceAdapters.h"
#include "ISortableProvider.h"

// Explicit registration function — called from Deki2D_RegisterComponents()
void Deki2D_RegisterSortingGroupAdapters()
{
    static bool s_registered = false;
    if (s_registered) return;
    s_registered = true;

    ComponentInterfaceAdapters::Register(
        ISortableProvider::InterfaceID,
        SortingGroupComponent::StaticType,
        [](DekiComponent* c) -> void* {
            return static_cast<ISortableProvider*>(static_cast<SortingGroupComponent*>(c));
        });
}

// Static init — works for DLL builds
static struct SortingGroupInterfaceRegistrar {
    SortingGroupInterfaceRegistrar() { Deki2D_RegisterSortingGroupAdapters(); }
} s_sortingGroupInterfaceRegistrar;
