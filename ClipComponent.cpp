#include "ClipComponent.h"
#include <deki/ComponentInterfaceAdapters.h>
#include <deki/IClipProvider.h>
#include <deki/ISortableProvider.h>

using namespace Deki2D;

// Explicit registration function — called from ::Deki2DRegisterComponents()
// to ensure interface adapters are registered even when the linker strips
// object files with only static initializers (e.g., ESP-IDF static libs).
void Deki2DRegisterClipAdapters()
{
    static bool s_Registered = false;
    if (s_Registered)
    {
        return;
    }
    s_Registered = true;

    Deki::ComponentInterfaceAdapters::Register(
        Deki::IClipProvider::kInterfaceID, ::Deki::TypeId<ClipComponent>(),
        [](Deki::Component* c) -> void* { return static_cast<Deki::IClipProvider*>(static_cast<ClipComponent*>(c)); });
    Deki::ComponentInterfaceAdapters::Register(
        Deki::ISortableProvider::kInterfaceID, ::Deki::TypeId<ClipComponent>(), [](Deki::Component* c) -> void*
        { return static_cast<Deki::ISortableProvider*>(static_cast<ClipComponent*>(c)); });
}

// Static init — works for DLL builds where all objects are loaded
static struct ClipInterfaceRegistrar
{
    ClipInterfaceRegistrar() { Deki2DRegisterClipAdapters(); }
} s_ClipInterfaceRegistrar;
