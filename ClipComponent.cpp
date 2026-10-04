#include "ClipComponent.h"
#include <deki/ComponentInterfaceAdapters.h>
#include <deki/IClipProvider.h>
#include <deki/ISortableProvider.h>

using namespace Deki2D;

// Called from ::Deki2DRegisterComponents(), so the interface adapters are
// registered even when the linker drops object files that hold only static
// initializers (as with ESP-IDF static libraries).
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

// Static init, for DLL builds, where every object file is loaded
static struct ClipInterfaceRegistrar
{
    ClipInterfaceRegistrar() { Deki2DRegisterClipAdapters(); }
} s_ClipInterfaceRegistrar;
