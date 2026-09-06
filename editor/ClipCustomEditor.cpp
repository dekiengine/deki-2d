/**
 * @file ClipCustomEditor.cpp
 * @brief Editor support for ClipComponent
 *
 * Provides display size info for gizmos/selection.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/EditorApplication.h>
#include "ClipComponent.h"
#include <deki/Engine.h>  // for DekiEngineSettings::Global()

namespace DekiEditor
{

class ClipCustomEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "ClipComponent"; }

    // Override the inspector to append an "Edit Rect" button below the standard
    // properties. The button activates the scene-view Rect tool so the clip's
    // width/height can be dragged directly on the canvas via the resize handles.
    bool WantsInspectorOverride(Deki::Component* comp) override { return comp != nullptr; }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        auto& ui = EditorUI::Get();

        // Standard width/height/sortingOrder fields.
        ui.DrawDefaultInspector();

        ui.Spacing();
        if (ui.Button("Edit Rect"))
        {
            // Drain happens in EditorApp; switches the scene view to the Rect
            // resize tool. The clip is already the selected object (it's the
            // one being inspected), so its resize handles appear immediately.
            EditorApplication::Get().RequestSceneTool(SceneEditTool::Rect);
        }
    }

    // GetDisplaySize's contract is pixels (gizmo/selection consumers in
    // SceneGizmos/SceneSelection treat the returned values as pixel
    // dimensions). ClipComponent stores meters, so convert via the project
    // pixels-per-meter. Mirrors what the renderer does with the camera PPM;
    // diverges only if a camera overrides its own pixelsPerMeter.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* clipComp = static_cast<ClipComponent*>(comp);
        if (!clipComp)
            return false;

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth  = clipComp->width  * effective;
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

} // namespace DekiEditor

#endif // DEKI_EDITOR
