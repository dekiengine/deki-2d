#pragma once

#include <stdint.h>
#include <functional>
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

using ToggleCallback = std::function<void(bool on)>;

/// An on/off switch: a knob that slides across a pill-shaped track, which
/// changes colour with it. A tap flips it, and so do Enter and Space while
/// it has keyboard focus.
///
/// Its parts are children the editor makes: Track, Knob and Focus.
///
/// Example:
///
///     auto* sound = entity->GetComponent<SwitchComponent>();
///     sound->AddOnToggledCallback([](bool on) { SetSound(on); });
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("An on/off switch with a sliding knob.")
class SwitchComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that catches the tap. The editor adds one on the same object and sizes it to the "
                 "switch.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP("Whether the switch is on.")
    bool isOn = false;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled switch ignores taps and keys.")
    bool isEnabled = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the switch in meters.")
    DEKI_UNIT(Distance)
    float width = 3.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the switch in meters.")
    DEKI_UNIT(Distance)
    float height = 1.6f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Track colour while on.")
    Deki::Color onColor = Deki::Color(64, 140, 255);

    DEKI_EXPORT
    DEKI_TOOLTIP("Track colour while off.")
    Deki::Color offColor = Deki::Color(58, 63, 75);

    DEKI_EXPORT
    DEKI_TOOLTIP("How fast the knob slides across. 0 jumps.")
    DEKI_RANGE(0, 60)
    float glideSpeed = 14.0f;

    // Runtime-only listeners
    std::vector<ToggleCallback> onToggled;

    SwitchComponent() = default;
    ~SwitchComponent() override;

    /// Turns it on or off. With `notify`, the toggled callbacks run.
    void SetOn(bool on, bool notify = true);

    /// Flips it, as a tap does.
    void Toggle() { SetOn(!isOn); }

    void AddOnToggledCallback(const ToggleCallback& callback);

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
        h = height;
    }
    void OnFocusChanged(bool focused) override;
    bool OnFocusKey(uint32_t key, uint32_t character) override;

private:
    Deki::Object* m_Track = nullptr;
    Deki::Object* m_Knob = nullptr;
    Deki::Object* m_Focus = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    float m_Shown = -1.0f;  // knob position, 0 off to 1 on; below 0 until placed
    bool m_Focused = false;
    bool m_Pressed = false;

    void EnsureParts();
    void LayoutParts();
};

}  // namespace Deki2D
