/**
 * @file ScrollElementCustomEditor.cpp
 * @brief Editor support for ScrollElement
 *
 * Provides display size info and rect tool resize support.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "modules/2d/ScrollElement.h"

namespace DekiEditor
{

class ScrollElementCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "ScrollElement"; }

    bool GetDisplaySize(DekiComponent* comp, float& outWidth, float& outHeight) override
    {
        auto* element = static_cast<ScrollElement*>(comp);
        if (!element)
            return false;

        outWidth = static_cast<float>(element->width);
        outHeight = static_cast<float>(element->height);
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
        auto* element = static_cast<ScrollElement*>(comp);
        if (element)
        {
            if (outWidth) *outWidth = &element->width;
            if (outHeight) *outHeight = &element->height;
        }
    }
};

REGISTER_EDITOR(ScrollElementCustomEditor)
REGISTER_CREATE_MENU_ITEM(ScrollComponent, "UI", "Scroll View", "ScrollView", "ScrollComponent")

} // namespace DekiEditor

#endif // DEKI_EDITOR
