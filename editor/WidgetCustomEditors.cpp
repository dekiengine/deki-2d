// Editor support for the UI widgets (Bar, Slider, Switch, Checkbox, LED, Arc,
// Spinner, Spinbox, Dropdown, Tab View, Message Box, Date Picker): their parts kept in step in edit mode, where
// RuntimeUpdate does not run; their size, so the scene view can select and outline the widget rather than one of its
// parts; and a line in the inspector that says where the looks are edited.

#ifdef DEKI_EDITOR

#include <deki-editor/CustomEditor.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/Localization.h>
#include <deki/Engine.h>
#include "ArcComponent.h"
#include "BarComponent.h"
#include "CheckboxComponent.h"
#include "DatePickerComponent.h"
#include "DropdownComponent.h"
#include "LedComponent.h"
#include "MsgboxComponent.h"
#include "SliderComponent.h"
#include "SpinboxComponent.h"
#include "SpinnerComponent.h"
#include "SwitchComponent.h"
#include "TabviewComponent.h"

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

class SpinboxEditor : public WidgetEditorOf<SpinboxComponent>
{
public:
    const char* GetComponentName() const override { return "SpinboxComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* spinbox = static_cast<SpinboxComponent*>(comp);
        w = spinbox->width;
        h = spinbox->height;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the child parts Field, Value, Minus, Plus, their labels, and Focus (shown while it "
                  "has keyboard focus).");
    }
};

class DropdownEditor : public WidgetEditorOf<DropdownComponent>
{
public:
    const char* GetComponentName() const override { return "DropdownComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* dropdown = static_cast<DropdownComponent*>(comp);
        w = dropdown->width;
        h = dropdown->height;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the child parts Field, Label, Arrow and Focus, and on the List's Back, Highlight "
                  "and rows. The list shows in play mode.");
    }
};

class TabviewEditor : public WidgetEditorOf<TabviewComponent>
{
public:
    const char* GetComponentName() const override { return "TabviewComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* tabview = static_cast<TabviewComponent*>(comp);
        w = tabview->width;
        h = tabview->height;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Put each tab's objects under its page: Page0, Page1 and so on. The bar's looks are on Bar, "
                  "Indicator and the Tab texts.");
    }
};

class MsgboxEditor : public WidgetEditorOf<MsgboxComponent>
{
public:
    const char* GetComponentName() const override { return "MsgboxComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* box = static_cast<MsgboxComponent*>(comp);
        w = box->width;
        h = box->height;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the parts under Box: Backdrop, Panel, Title, Message and the buttons. Tick Is "
                  "Open to see it while editing.");
    }
};

class DatePickerEditor : public WidgetEditorOf<DatePickerComponent>
{
public:
    const char* GetComponentName() const override { return "DatePickerComponent"; }

protected:
    bool SizeOf(Deki::Component* comp, float& w, float& h) override
    {
        auto* picker = static_cast<DatePickerComponent*>(comp);
        w = picker->width;
        h = picker->rowHeight * 3.25f;
        return true;
    }
    const char* PartsHint() const override
    {
        return Tr("Its looks are on the child parts Back and Band, and on the Day, Month and Year rollers.");
    }
};

REGISTER_EDITOR(BarEditor)
REGISTER_EDITOR(SliderEditor)
REGISTER_EDITOR(SwitchEditor)
REGISTER_EDITOR(CheckboxEditor)
REGISTER_EDITOR(LedEditor)
REGISTER_EDITOR(ArcEditor)
REGISTER_EDITOR(SpinnerEditor)
REGISTER_EDITOR(SpinboxEditor)
REGISTER_EDITOR(DropdownEditor)
REGISTER_EDITOR(TabviewEditor)
REGISTER_EDITOR(MsgboxEditor)
REGISTER_EDITOR(DatePickerEditor)

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
