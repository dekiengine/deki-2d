/**
 * @file ScrollCustomEditor.cpp
 * @brief Editor support for ScrollComponent
 *
 * Handles edit-mode child distribution by lazily syncing layout
 * when the Clip child count changes.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include "modules/2d/ScrollComponent.h"
#include "DekiObject.h"

namespace DekiEditor
{

class ScrollCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "ScrollComponent"; }

    void OnEditorUpdate(DekiComponent* comp) override
    {
        auto* scroll = static_cast<ScrollComponent*>(comp);
        if (!scroll) return;

        DekiObject* owner = scroll->GetOwner();
        if (!owner) return;

        // Find Clip child and count its children
        int childCount = 0;
        for (auto* child : owner->GetChildren())
        {
            if (child->GetName() == "Clip")
            {
                childCount = static_cast<int>(child->GetChildren().size());
                break;
            }
        }

        // Sync layout if child count changed or first time
        if (childCount != m_LastChildCount)
        {
            m_LastChildCount = childCount;
            scroll->SyncChildObjects(owner);
        }
    }

private:
    int m_LastChildCount = -1;  // -1 forces first sync
};

REGISTER_EDITOR(ScrollCustomEditor)

} // namespace DekiEditor

#endif // DEKI_EDITOR
