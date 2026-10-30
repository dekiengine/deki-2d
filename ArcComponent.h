#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

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

/// A round gauge or dial: the value is the stretch of a ring from the start
/// angle, clockwise. Drag the knob round, or step it with Left and Right
/// while it has keyboard focus. With interaction off it is a read-only
/// gauge: a speedometer, a battery ring, a timer.
///
/// Its parts are children the editor makes: Track (the whole ring),
/// Indicator (up to the value), Knob and Focus.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A round gauge or dial: a value shown as a stretch of a ring.")
class ArcComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that catches the drag. The editor adds one on the same object and sizes it to the "
                 "ring.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP("The value shown, between Min Value and Max Value.")
    float value = 40.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("The value at the start of the ring.")
    float minValue = 0.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("The value at the end of the ring.")
    float maxValue = 100.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Values snap to multiples of this from Min Value. 0 turns freely.")
    float step = 0.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Diameter of the ring in meters.")
    DEKI_UNIT(Distance)
    float size = 5.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Thickness of the ring in meters.")
    DEKI_UNIT(Distance)
    float thickness = 0.6f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Where the ring starts: the minimum. 0 points right; 225 degrees is the lower left, as on a gauge.")
    DEKI_UNIT(Angle)
    float startAngle = 3.9269908f;

    DEKI_EXPORT
    DEKI_TOOLTIP("How far round the ring runs clockwise from the start. 270 degrees leaves a gap at the bottom.")
    DEKI_UNIT(Angle)
    float sweepAngle = 4.7123890f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Lets the value be dragged and stepped with the keys. Off, the arc is a read-only gauge with no "
                 "knob.")
    bool interactive = true;

    // Runtime-only listeners
    std::vector<std::function<void(float)>> onValueChanged;

    ArcComponent() = default;
    ~ArcComponent() override;

    /// Sets the value, kept between the minimum and the maximum.
    void SetValue(float newValue);

    /// Where the value sits between minimum and maximum, 0 to 1.
    float GetFraction() const;

    void AddOnValueChangedCallback(const std::function<void(float)>& callback);

    /// The value at a point in the world, from its angle round the centre.
    /// `previous` breaks the tie in the gap: a drag past an end stays at it.
    float ValueAtPoint(float worldX, float worldY, float previous) const;

    /// Places and sizes the parts, making any the editor is missing.
    void SyncParts();

    void Start() override;
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

    // IFocusable
    Deki::Object* GetFocusObject() const override { return GetOwner(); }
    bool CanFocus() const override { return interactive; }
    void GetFocusSize(float& w, float& h) const override
    {
        w = size;
        h = size;
    }
    void OnFocusChanged(bool focused) override;
    bool OnFocusKey(uint32_t key, uint32_t character) override;

private:
    Deki::Object* m_Track = nullptr;
    Deki::Object* m_Indicator = nullptr;
    Deki::Object* m_Knob = nullptr;
    Deki::Object* m_Focus = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    bool m_Focused = false;
    bool m_Dragging = false;

    void EnsureParts();
    void LayoutParts();
    void SetValueFromInput(float newValue);
};

}  // namespace Deki2D
