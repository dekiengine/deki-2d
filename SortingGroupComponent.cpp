#include "SortingGroupComponent.h"
#include <deki/ComponentInterfaceAdapters.h>
#include <deki/ISortableProvider.h>

using namespace Deki2D;

// Called from ::Deki2DRegisterComponents().
void Deki2DRegisterSortingGroupAdapters()
{
    static bool s_Registered = false;
    if (s_Registered)
    {
        return;
    }
    s_Registered = true;

    Deki::ComponentInterfaceAdapters::Register(
        Deki::ISortableProvider::kInterfaceID, ::Deki::TypeId<SortingGroupComponent>(), [](Deki::Component* c) -> void*
        { return static_cast<Deki::ISortableProvider*>(static_cast<SortingGroupComponent*>(c)); });
}

// Registers at static init too, which covers DLL builds.
static struct SortingGroupInterfaceRegistrar
{
    SortingGroupInterfaceRegistrar() { Deki2DRegisterSortingGroupAdapters(); }
} s_SortingGroupInterfaceRegistrar;
