/**
 * @file ButtonCustomEditor.cpp
 * @brief Editor support for ButtonComponent
 *
 * Provides display size, hit testing, and resize support so the button's
 * hit area is visible and editable in the prefab view.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "ButtonComponent.h"
#include "deki-input/InputCollider.h"

namespace DekiEditor
{

class ButtonCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override
    {
        return "ButtonComponent";
    }

    bool GetDisplaySize(DekiComponent* comp, float& outWidth, float& outHeight) override
    {
        auto* button = static_cast<ButtonComponent*>(comp);
        if (!button)
            return false;

        InputCollider* collider = button->input_collider.Get();
        if (!collider)
            return false;

        outWidth = static_cast<float>(collider->width);
        outHeight = static_cast<float>(collider->height);

        return outWidth > 0 && outHeight > 0;
    }

    bool HitTest(DekiComponent* comp, float localX, float localY, float width, float height) override
    {
        auto* button = static_cast<ButtonComponent*>(comp);
        if (!button)
            return false;

        InputCollider* collider = button->input_collider.Get();
        if (!collider)
            return false;

        Bounds2D bounds = collider->GetBounds();
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;

        // Expand hit area by padding
        float left = -halfW - bounds.padding_left;
        float right = halfW + bounds.padding_right;
        float top = -halfH - bounds.padding_top;
        float bottom = halfH + bounds.padding_bottom;

        return (localX >= left && localX <= right && localY >= top && localY <= bottom);
    }

    bool SupportsResize() const override
    {
        return true;
    }

    void GetResizeTarget(DekiComponent* comp, int32_t** outWidth, int32_t** outHeight) override
    {
        auto* button = static_cast<ButtonComponent*>(comp);
        if (!button)
            return;

        InputCollider* collider = button->input_collider.Get();
        if (!collider)
            return;

        if (outWidth) *outWidth = &collider->width;
        if (outHeight) *outHeight = &collider->height;
    }
};

REGISTER_EDITOR(ButtonCustomEditor)
REGISTER_CREATE_MENU_ITEM(ButtonComponent, "UI", "Button", "Button", "ButtonComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
