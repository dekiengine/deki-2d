#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include "BarComponent.h"
#include "FocusSystem.h"
#include <deki/reflection/ObjectRef.h>

namespace DekiInput
{
class InputCollider;
}

namespace Deki2D
{

using ValueCallback = std::function<void(float value)>;

/// A bar with a knob to drag: volume, brightness, difficulty.
///
/// Drag the knob or tap the track to set the value. With keyboard focus,
/// Left and Right (Down and Up for a vertical slider) step it. On top of
/// the bar's Track and Fill it places a Knob and a Focus ring, all child
/// parts the editor makes.
///
/// Example:
///
///     auto* slider = entity->GetComponent<SliderComponent>();
///     slider->AddOnValueChangedCallback([](float v) { SetVolume(v); });
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A bar with a knob to drag or step with the keys: volume, brightness.")
class SliderComponent : public BarComponent, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that catches the drag. The editor adds one on the same object and keeps it sized to "
                 "the slider.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP("Values snap to multiples of this from Min Value. 0 slides freely.")
    float step = 0.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("How far one key press moves the value. 0 moves it a twentieth of the range, or one step.")
    float keyStep = 0.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Diameter of the knob in meters.")
    DEKI_UNIT(Distance)
    float knobSize = 1.5f;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled slider ignores touch and keys.")
    bool isEnabled = true;

    // Runtime-only listeners
    std::vector<ValueCallback> onValueChanged;
    std::vector<ValueCallback> onValueCommitted;

    SliderComponent() = default;
    ~SliderComponent() override;

    void Start() override;

    /// Called whenever the value changes, during a drag too.
    void AddOnValueChangedCallback(const ValueCallback& callback);

    /// Called when a drag ends or a key press changes the value.
    void AddOnValueCommittedCallback(const ValueCallback& callback);

    /// Sets the value as a user would: snapped to the step, and announced.
    void SetValueFromInput(float newValue);

    // IFocusable
    Deki::Object* GetFocusObject() const override { return GetOwner(); }
    bool CanFocus() const override { return isEnabled; }
    void GetFocusSize(float& w, float& h) const override;
    void OnFocusChanged(bool focused) override;
    bool OnFocusKey(uint32_t key, uint32_t character) override;

protected:
    void EnsureParts() override;
    void LayoutParts(float shown) override;
    uint64_t LayoutKey() const override;

private:
    Deki::Object* m_Knob = nullptr;
    Deki::Object* m_Focus = nullptr;
    bool m_Dragging = false;
    bool m_Focused = false;

    float Snap(float v) const;
    float ValueAt(float worldX, float worldY) const;
    void Announce(bool committed);
};

}  // namespace Deki2D
