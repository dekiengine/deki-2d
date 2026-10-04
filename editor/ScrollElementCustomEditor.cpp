// Editor support for ScrollElement: its display size.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "ScrollElement.h"
#include <deki/Engine.h>

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

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
        {
            return false;
        }

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth = element->width * effective;
        outHeight = element->height * effective;
        return true;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    // No resize gizmo: ScrollElement.width/height are floats (world meters),
    // but GetResizeTarget hands the gizmo int32_t* fields. Until the editor
    // API has a float variant, the element is sized in the Inspector.
};

REGISTER_EDITOR(ScrollElementCustomEditor)
REGISTER_CREATE_MENU_ITEM(ScrollComponent, "UI", "Scroll View", "ScrollView", "ScrollComponent")

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
