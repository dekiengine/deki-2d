// Editor support for ClipComponent: display size for gizmos and selection,
// and an Edit Rect button.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/EditorApplication.h>
#include "ClipComponent.h"
#include <deki/Engine.h>  // for DekiEngineSettings::Global()

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

namespace DekiEditor
{

class ClipCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "ClipComponent"; }

    // Adds an "Edit Rect" button below the standard properties. It switches
    // the scene view to the Rect tool, so the clip's width/height can be
    // dragged on the canvas with the resize handles.
    bool WantsInspectorOverride(Deki::Component* comp) override { return comp != nullptr; }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        auto& ui = EditorUI::Get();

        // Standard width/height/sortingOrder fields.
        ui.DrawDefaultInspector();

        ui.Spacing();
        if (ui.Button("Edit Rect"))
        {
            // EditorApp carries out the request. The clip is already selected
            // (it is the one being inspected), so its handles show at once.
            EditorApplication::Get().RequestSceneTool(SceneEditTool::Rect);
        }
    }

    // GetDisplaySize returns pixels (SceneGizmos and SceneSelection read them
    // as pixels). ClipComponent stores meters, so convert with the project's
    // pixels per meter.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* clipComp = static_cast<ClipComponent*>(comp);
        if (!clipComp)
        {
            return false;
        }

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth = clipComp->width * effective;
        outHeight = clipComp->height * effective;
        return true;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }
};

REGISTER_EDITOR(ClipCustomEditor)

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
