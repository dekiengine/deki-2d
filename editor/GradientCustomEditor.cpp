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
#include <deki/Engine.h>

namespace DekiEditor
{

class GradientCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override
    {
        return "GradientComponent";
    }

    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* gradient = static_cast<GradientComponent*>(comp);
        if (!gradient)
            return false;

        // width/height are world meters; display bounds want pixels.
        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        outWidth = gradient->width * ppm;
        outHeight = gradient->height * ppm;
        return true;
    }
};

REGISTER_EDITOR(GradientCustomEditor)
REGISTER_CREATE_MENU_ITEM(GradientComponent, "2D", "Gradient", "Gradient", "GradientComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
