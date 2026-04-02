/**
 * @file ClipCustomEditor.cpp
 * @brief Editor support for ClipComponent
 *
 * Provides display size info for gizmos/selection.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "modules/2d/ClipComponent.h"

namespace DekiEditor
{

class ClipCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "ClipComponent"; }

    bool GetDisplaySize(DekiComponent* comp, float& outWidth, float& outHeight) override
    {
        auto* clipComp = static_cast<ClipComponent*>(comp);
        if (!clipComp)
            return false;

        outWidth = static_cast<float>(clipComp->width);
        outHeight = static_cast<float>(clipComp->height);
        return true;
    }

    bool HitTest(DekiComponent* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    bool SupportsResize() const override
    {
        return true;
    }

    void GetResizeTarget(DekiComponent* comp, int32_t** outWidth, int32_t** outHeight) override
    {
        auto* clipComp = static_cast<ClipComponent*>(comp);
        if (clipComp)
        {
            if (outWidth) *outWidth = &clipComp->width;
            if (outHeight) *outHeight = &clipComp->height;
        }
    }
};

REGISTER_EDITOR(ClipCustomEditor)

} // namespace DekiEditor

#endif // DEKI_EDITOR
