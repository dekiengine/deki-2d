// Editor support for ScrollComponent. Keeps the hierarchy right in edit mode:
// makes sure Clip exists, and in Template mode shows only the Template child
// (instantiated from itemScene when needed), so the user sees exactly the
// item being authored. The runtime-only Slot pool is removed in edit mode and
// rebuilt at Play.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorComponents.h>
#include "ScrollComponent.h"
#include <deki/Object.h>
#include <deki/Scene.h>

namespace DekiEditor
{

namespace
{

Deki::Object* FindChildByName(Deki::Object* parent, const char* name)
{
    if (!parent)
    {
        return nullptr;
    }
    for (auto* child : parent->GetChildren())
    {
        if (child->GetName() == name)
        {
            return child;
        }
    }
    return nullptr;
}

Deki::Object* EnsureChild(Deki::Object* parent, const char* name, const char* componentType)
{
    if (!parent)
    {
        return nullptr;
    }
    if (Deki::Object* found = FindChildByName(parent, name))
    {
        return found;
    }

    Deki::Object* child = new Deki::Object(name);
    parent->AddChild(child);
    if (componentType)
    {
        DekiEditor::AddComponentByName(child, componentType);
    }
    return child;
}

Deki::Object* InstantiateTemplateFromScene(Deki::Object* clip, Deki::Scene* itemScene)
{
    if (!clip || !itemScene)
    {
        return nullptr;
    }

    Deki::Scene* ownerScene = clip->GetOwnerScene();
    if (!ownerScene)
    {
        return nullptr;
    }

    Deki::Object* instance = itemScene->Instantiate(ownerScene);
    if (!instance)
    {
        return nullptr;
    }

    clip->AddChild(instance);
    instance->SetName("Template");
    return instance;
}

void DeleteSlotChildren(Deki::Object* clip)
{
    if (!clip)
    {
        return;
    }
    auto children = clip->GetChildren();  // copy
    for (auto* child : children)
    {
        if (child->GetName().rfind("Slot", 0) == 0)
        {
            clip->RemoveChild(child);
            delete child;
        }
    }
}

void DeleteChild(Deki::Object* parent, Deki::Object* child)
{
    if (!parent || !child)
    {
        return;
    }
    parent->RemoveChild(child);
    delete child;
}

// The scene identity used to compare scroll->itemScene with an existing
// Template child. AssetRef carries a runtime guid and an editor-side source
// guid; the source when set, else the guid.
const std::string& SceneIdentity(const Deki::AssetRef<Deki::Scene>& ref)
{
    return !ref.source.empty() ? ref.source : ref.guid;
}

}  // namespace

class ScrollCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "ScrollComponent"; }

    void OnEditorUpdate(Deki::Component* comp) override
    {
        auto* scroll = static_cast<ScrollComponent*>(comp);
        if (!scroll)
        {
            return;
        }

        Deki::Object* owner = scroll->GetOwner();
        if (!owner)
        {
            return;
        }

        Deki::Object* clip = EnsureChild(owner, "Clip", "ClipComponent");
        if (!clip)
        {
            return;
        }

        if (scroll->mode == ScrollMode::Template)
        {
            EnsureTemplatePreview(scroll, clip);
        }
        else
        {
            SyncNonTemplate(scroll, owner, clip);
        }
    }

private:
    void EnsureTemplatePreview(ScrollComponent* scroll, Deki::Object* clip)
    {
        Deki::Scene* itemScene = scroll->itemScene.Get();
        const std::string& selectedGuid = SceneIdentity(scroll->itemScene);

        Deki::Object* tmpl = FindChildByName(clip, "Template");

        // If itemScene now names another scene (or none), the Template is
        // stale: rebuild it. The Template carries its source scene GUID from
        // instantiation, so later ticks (and later sessions, through
        // ExpandInstance on load) can detect the mismatch.
        if (tmpl && tmpl->GetSourceSceneGuid() != selectedGuid)
        {
            DeleteChild(clip, tmpl);
            tmpl = nullptr;
        }

        if (!tmpl && itemScene && !selectedGuid.empty())
        {
            tmpl = InstantiateTemplateFromScene(clip, itemScene);
            if (tmpl)
            {
                tmpl->SetSourceSceneGuid(selectedGuid);
            }
        }

        if (tmpl)
        {
            tmpl->SetActive(true);
            tmpl->SetLocalPosition(0.0f, 0.0f);
        }

        DeleteSlotChildren(clip);
        m_LastChildCount = -1;  // resync if the user switches back to NonTemplate
    }

    void SyncNonTemplate(ScrollComponent* scroll, Deki::Object* owner, Deki::Object* clip)
    {
        int childCount = static_cast<int>(clip->GetChildren().size());
        if (childCount != m_LastChildCount)
        {
            m_LastChildCount = childCount;
            scroll->SyncChildObjects(owner);
        }
    }

    int m_LastChildCount = -1;
};

REGISTER_EDITOR(ScrollCustomEditor)

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
