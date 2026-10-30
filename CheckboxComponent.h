#pragma once

#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

#include <deki/Color.h>
#include <deki/Component.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/Property.h>
#include "FocusSystem.h"

namespace Deki
{
class Object;
}

namespace DekiInput
{
class InputCollider;
}

namespace Deki2D
{

using CheckCallback = std::function<void(bool checked)>;

/// A box that ticks on and off, with a label beside it. A tap anywhere on
/// it flips it, and so do Enter and Space while it has keyboard focus.
///
/// Its parts are children the editor makes: Box, Check (the tick), Label
/// and Focus.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A box that ticks on and off, with a label.")
class CheckboxComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that catches the tap. The editor adds one on the same object and sizes it to the "
                 "box and label.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP("Whether the box is ticked.")
    bool isChecked = false;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled checkbox ignores taps and keys.")
    bool isEnabled = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("The text beside the box.")
    std::string label = "Checkbox";

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the box and its label together, in meters.")
    DEKI_UNIT(Distance)
    float width = 8.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Size of the box in meters. The checkbox is this tall.")
    DEKI_UNIT(Distance)
    float boxSize = 1.5f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Box colour while ticked.")
    Deki::Color checkedColor = Deki::Color(64, 140, 255);

    DEKI_EXPORT
    DEKI_TOOLTIP("Outline colour while not ticked.")
    Deki::Color uncheckedColor = Deki::Color(140, 146, 160);

    // Runtime-only listeners
    std::vector<CheckCallback> onToggled;

    CheckboxComponent() = default;
    ~CheckboxComponent() override;

    /// Ticks or clears it. With `notify`, the toggled callbacks run.
    void SetChecked(bool checked, bool notify = true);

    void Toggle() { SetChecked(!isChecked); }

    void AddOnToggledCallback(const CheckCallback& callback);

    /// Places and sizes the parts, making any the editor is missing.
    void SyncParts();

    void Start() override;
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

    // IFocusable
    Deki::Object* GetFocusObject() const override { return GetOwner(); }
    bool CanFocus() const override { return isEnabled; }
    void GetFocusSize(float& w, float& h) const override
    {
        w = width;
        h = boxSize;
    }
    void OnFocusChanged(bool focused) override;
    bool OnFocusKey(uint32_t key, uint32_t character) override;

private:
    Deki::Object* m_Box = nullptr;
    Deki::Object* m_Check = nullptr;
    Deki::Object* m_Label = nullptr;
    Deki::Object* m_Focus = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    bool m_Focused = false;
    bool m_Pressed = false;

    void EnsureParts();
    void LayoutParts();
};

}  // namespace Deki2D
