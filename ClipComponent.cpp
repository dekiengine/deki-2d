#include "ClipComponent.h"
#include <deki/ComponentInterfaceAdapters.h>
#include <deki/IClipProvider.h>
#include <deki/ISortableProvider.h>

// Explicit registration function — called from Deki2D_RegisterComponents()
// to ensure interface adapters are registered even when the linker strips
// object files with only static initializers (e.g., ESP-IDF static libs).
void Deki2D_RegisterClipAdapters()
{
    static bool s_registered = false;
    if (s_registered) return;
    s_registered = true;

    Deki::ComponentInterfaceAdapters::Register(
        Deki::IClipProvider::InterfaceID,
        ::Deki::TypeId<ClipComponent>(),
        [](Deki::Component* c) -> void* {
            return static_cast<Deki::IClipProvider*>(static_cast<ClipComponent*>(c));
        });
    Deki::ComponentInterfaceAdapters::Register(
        Deki::ISortableProvider::InterfaceID,
        ::Deki::TypeId<ClipComponent>(),
        [](Deki::Component* c) -> void* {
            return static_cast<Deki::ISortableProvider*>(static_cast<ClipComponent*>(c));
        });
}

// Static init — works for DLL builds where all objects are loaded
static struct ClipInterfaceRegistrar {
    ClipInterfaceRegistrar() { Deki2D_RegisterClipAdapters(); }
} s_clipInterfaceRegistrar;
