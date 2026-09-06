/**
 * @file ScrollElementCustomEditor.cpp
 * @brief Editor support for ScrollElement
 *
 * Provides display size info and rect tool resize support.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "ScrollElement.h"
#include <deki/Engine.h>

namespace DekiEditor
{

class ScrollElementCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "ScrollElement"; }

    // width/height are world meters; gizmo consumers want pixels.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* element = static_cast<ScrollElement*>(comp);
        if (!element)
            return false;

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth  = element->width  * effective;
        outHeight = element->height * effective;
        return true;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    // Resize gizmo not exposed: ScrollElement.width/height are now float
    // (world meters), but GetResizeTarget hands the gizmo int32_t* fields.
    // Until the editor API gains a float variant, the element is sized via
    // the inspector instead.
};

REGISTER_EDITOR(ScrollElementCustomEditor)
REGISTER_CREATE_MENU_ITEM(ScrollComponent, "UI", "Scroll View", "ScrollView", "ScrollComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
