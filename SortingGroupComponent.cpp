#include "SortingGroupComponent.h"
#include <deki/ComponentInterfaceAdapters.h>
#include <deki/ISortableProvider.h>

// Explicit registration function — called from Deki2D_RegisterComponents()
void Deki2D_RegisterSortingGroupAdapters()
{
    static bool s_registered = false;
    if (s_registered) return;
    s_registered = true;

    Deki::ComponentInterfaceAdapters::Register(
        Deki::ISortableProvider::InterfaceID,
        SortingGroupComponent::StaticType,
        [](Deki::Component* c) -> void* {
            return static_cast<Deki::ISortableProvider*>(static_cast<SortingGroupComponent*>(c));
        });
}

// Static init — works for DLL builds
static struct SortingGroupInterfaceRegistrar {
    SortingGroupInterfaceRegistrar() { Deki2D_RegisterSortingGroupAdapters(); }
} s_sortingGroupInterfaceRegistrar;
