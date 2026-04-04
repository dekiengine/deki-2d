/**
 * @file GradientCustomEditor.cpp
 * @brief Editor support for GradientComponent
 *
 * Provides display size info for gizmos/selection.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "GradientComponent.h"

namespace DekiEditor
{

class GradientCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override
    {
        return "GradientComponent";
    }

    bool GetDisplaySize(DekiComponent* comp, float& outWidth, float& outHeight) override
    {
        auto* gradient = static_cast<GradientComponent*>(comp);
        if (!gradient)
            return false;

        outWidth = static_cast<float>(gradient->width);
        outHeight = static_cast<float>(gradient->height);
        return true;
    }
};

REGISTER_EDITOR(GradientCustomEditor)
REGISTER_CREATE_MENU_ITEM(GradientComponent, "2D", "Gradient", "Gradient", "GradientComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
