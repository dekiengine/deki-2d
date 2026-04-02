#include "ClipComponent.h"
#include "../rendering/ComponentInterfaceAdapters.h"
#include "../rendering/IClipProvider.h"
#include "../rendering/ISortableProvider.h"

// Register interface adapters at static init — no RTTI, no DekiComponent vtable changes
static struct ClipInterfaceRegistrar {
    ClipInterfaceRegistrar() {
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
} s_clipInterfaceRegistrar;
