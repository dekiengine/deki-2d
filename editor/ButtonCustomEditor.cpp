/**
 * @file ButtonCustomEditor.cpp
 * @brief Editor support for ButtonComponent
 *
 * Provides display size, hit testing, and resize support so the button's
 * hit area is visible and editable in the scene view.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "ButtonComponent.h"
#include "deki-input/InputCollider.h"
#include <deki/Engine.h>

namespace DekiEditor
{

class ButtonCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override
    {
        return "ButtonComponent";
    }

    // collider width/height are world meters; gizmo consumers want pixels.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* button = static_cast<ButtonComponent*>(comp);
        if (!button)
            return false;

        InputCollider* collider = button->inputCollider.Get();
        if (!collider)
            return false;

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth  = collider->width  * effective;
        outHeight = collider->height * effective;
        return outWidth > 0 && outHeight > 0;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        auto* button = static_cast<ButtonComponent*>(comp);
        if (!button)
            return false;

        InputCollider* collider = button->inputCollider.Get();
        if (!collider)
            return false;

        Bounds2D bounds = collider->GetBounds();
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;

        // Expand hit area by padding
        float left = -halfW - bounds.paddingLeft;
        float right = halfW + bounds.paddingRight;
        float top = -halfH - bounds.paddingTop;
        float bottom = halfH + bounds.paddingBottom;

        return (localX >= left && localX <= right && localY >= top && localY <= bottom);
    }

    // Resize gizmo not exposed: the button's hit area lives on InputCollider,
    // whose width/height are float (world units), but GetResizeTarget hands the
    // gizmo int32_t* fields. Until the editor API gains a float variant, the
    // collider is sized via the inspector instead.
};

REGISTER_EDITOR(ButtonCustomEditor)
REGISTER_CREATE_MENU_ITEM(ButtonComponent, "UI", "Button", "Button", "ButtonComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
