#include "ClipComponent.h"
#include "ComponentInterfaceAdapters.h"
#include "IClipProvider.h"
#include "ISortableProvider.h"

// Explicit registration function — called from Deki2D_RegisterComponents()
// to ensure interface adapters are registered even when the linker strips
// object files with only static initializers (e.g., ESP-IDF static libs).
void Deki2D_RegisterClipAdapters()
{
    static bool s_registered = false;
    if (s_registered) return;
    s_registered = true;

    ComponentInterfaceAdapters::Register(
        IClipProvider::InterfaceID,
        ClipComponent::StaticType,
        [](DekiComponent* c) -> void* {
            return static_cast<IClipProvider*>(static_cast<ClipComponent*>(c));
        });
    ComponentInterfaceAdapters::Register(
        ISortableProvider::InterfaceID,
        ClipComponent::StaticType,
        [](DekiComponent* c) -> void* {
            return static_cast<ISortableProvider*>(static_cast<ClipComponent*>(c));
        });
}

// Static init — works for DLL builds where all objects are loaded
static struct ClipInterfaceRegistrar {
    ClipInterfaceRegistrar() { Deki2D_RegisterClipAdapters(); }
} s_clipInterfaceRegistrar;
