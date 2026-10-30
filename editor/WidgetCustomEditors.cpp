// Editor support for the UI widgets (Bar, Slider, Switch, Checkbox, LED, Arc,
// Spinner): their parts kept in step in edit mode, where RuntimeUpdate does
// not run; their size, so the scene view can select and outline the widget
// rather than one of its parts; and a line in the inspector that says where
// the looks are edited.

#ifdef DEKI_EDITOR

#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/Localization.h>
#include <deki/Engine.h>
#include "ArcComponent.h"
#include "BarComponent.h"
#include "CheckboxComponent.h"
#include "LedComponent.h"
#include "SliderComponent.h"
#include "SpinnerComponent.h"
#include "SwitchComponent.h"

#include <algorithm>

using namespace Deki2D;

namespace DekiEditor
{

// A widget's size in meters, and the parts its looks live on.
class WidgetEditor : public CustomEditor
{
public:
    bool WantsInspectorOverride(Deki::Component* comp) override { return comp != nullptr; }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        (void)comp;
        auto& ui = EditorUI::Get();
        ui.DrawDefaultInspector();
        ui.Hint(PartsHint());
    }

    // Pixels, as SceneGizmos and SceneSelection read them.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        float w = 0.0f;
        float h = 0.0f;
        if (!comp || !SizeOf(comp, w, h))
        {
            return false;
        }
        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth = w * effective;
        outHeight = h * effective;
        return true;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        (void)comp;
        return localX >= -width * 0.5f && localX <= width * 0.5f && localY >= -height * 0.5f && localY <= height * 0.5f;
    }

protected:
    virtual bool SizeOf(Deki::Component* comp, float& w, float& h) = 0;
    // Through Tr(), with the text written at the call so the translation
    // check finds it.
    virtual const char* PartsHint() const = 0;
};

// Makes and places the parts of a T every frame of edit mode. A widget does
// nothing when nothing changed.
template <typename T>
class WidgetEditorOf : public WidgetEditor
{
public:
    void OnEditorUpdate(Deki::Component* comp) override
    {
        if (comp)
        {
            static_cast<T*>(comp)->SyncParts();
        }
    }
};

class BarEditor : public WidgetEditorOf<BarComponent>
{
public:
    const char* GetComponentName() const override { return "BarComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* bar = static_cast<BarComponent*>(comp);
        w = bar->width;
        h = bar->height;
        return true;
    }
    const char* PartsHint() const override { return Tr("Its looks are on the child parts Track and Fill."); }
};

class SliderEditor : public WidgetEditorOf<SliderComponent>
{
public:
    const char* GetComponentName() const override { return "SliderComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* slider = static_cast<SliderComponent*>(comp);
        const bool horizontal = slider->orientation == BarOrientation::Horizontal;
        w = horizontal ? slider->width + slider->knobSize : std::max(slider->width, slider->knobSize);
        h = horizontal ? std::max(slider->height, slider->knobSize) : slider->height + slider->knobSize;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the child parts Track, Fill, Knob and Focus (shown while it has keyboard focus).");
    }
};

class SwitchEditor : public WidgetEditorOf<SwitchComponent>
{
public:
    const char* GetComponentName() const override { return "SwitchComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* toggle = static_cast<SwitchComponent*>(comp);
        w = toggle->width;
        h = toggle->height;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the child parts Track, Knob and Focus (shown while it has keyboard focus).");
    }
};

class CheckboxEditor : public WidgetEditorOf<CheckboxComponent>
{
public:
    const char* GetComponentName() const override { return "CheckboxComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* box = static_cast<CheckboxComponent*>(comp);
        w = box->width;
        h = box->boxSize;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the child parts Box, Check, Label and Focus (shown while it has keyboard focus).");
    }
};

class LedEditor : public WidgetEditorOf<LedComponent>
{
public:
    const char* GetComponentName() const override { return "LedComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* led = static_cast<LedComponent*>(comp);
        w = led->size;
        h = led->size;
        return true;
    }
    const char* PartsHint() const override { return Tr("Its looks are on the child parts Glow and Body."); }
};

class ArcEditor : public WidgetEditorOf<ArcComponent>
{
public:
    const char* GetComponentName() const override { return "ArcComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* arc = static_cast<ArcComponent*>(comp);
        w = arc->size;
        h = arc->size;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the child parts Track, Indicator, Knob and Focus (shown while it has keyboard "
                  "focus).");
    }
};

class SpinnerEditor : public WidgetEditorOf<SpinnerComponent>
{
public:
    const char* GetComponentName() const override { return "SpinnerComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* spinner = static_cast<SpinnerComponent*>(comp);
        w = spinner->size;
        h = spinner->size;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the child parts Track and Arc. It turns in play mode.");
    }
};

REGISTER_EDITOR(BarEditor)
REGISTER_EDITOR(SliderEditor)
REGISTER_EDITOR(SwitchEditor)
REGISTER_EDITOR(CheckboxEditor)
REGISTER_EDITOR(LedEditor)
REGISTER_EDITOR(ArcEditor)
REGISTER_EDITOR(SpinnerEditor)

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
