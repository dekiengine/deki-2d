// Editor support for ButtonComponent: display size and hit testing, so the
// button's hit area shows in the scene view.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "ButtonComponent.h"
#include "deki-input/InputCollider.h"
#include <deki/Engine.h>

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

namespace DekiEditor
{

class ButtonCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "ButtonComponent"; }

    // The collider's width/height are world meters; the gizmos want pixels.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* button = static_cast<ButtonComponent*>(comp);
        if (!button)
        {
            return false;
        }

        DekiInput::InputCollider* collider = button->inputCollider.Get();
        if (!collider)
        {
            return false;
        }

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth = collider->width * effective;
        outHeight = collider->height * effective;
        return outWidth > 0 && outHeight > 0;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        auto* button = static_cast<ButtonComponent*>(comp);
        if (!button)
        {
            return false;
        }

        DekiInput::InputCollider* collider = button->inputCollider.Get();
        if (!collider)
        {
            return false;
        }

        float halfW = width * 0.5f;
        float halfH = height * 0.5f;

        // The padding widens the hit area.
        float left = -halfW - collider->paddingLeft;
        float right = halfW + collider->paddingRight;
        float top = -halfH - collider->paddingTop;
        float bottom = halfH + collider->paddingBottom;

        return (localX >= left && localX <= right && localY >= top && localY <= bottom);
    }

    // No resize gizmo: the hit area is the InputCollider's float width/height
    // (world units), but GetResizeTarget hands the gizmo int32_t* fields.
    // Until the editor API has a float variant, the collider is sized in the
    // inspector.
};

REGISTER_EDITOR(ButtonCustomEditor)
REGISTER_CREATE_MENU_ITEM(ButtonComponent, "UI", "Button", "Button", "ButtonComponent")

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
