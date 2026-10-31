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

/// A number with a minus button on the left and a plus button on the right:
/// a quantity, a level, a volume step. Holding a button repeats it. With
/// keyboard focus, Left and Right step it.
///
/// Its parts are children the editor makes: Field, Value, Minus,
/// MinusLabel, Plus, PlusLabel and Focus.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A number with minus and plus buttons.")
class SpinboxComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that catches presses on the buttons. The editor adds one on the same object and sizes "
                 "it to the spinbox.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP("The number shown, between Min Value and Max Value.")
    float value = 0.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("The lowest number.")
    float minValue = 0.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("The highest number.")
    float maxValue = 99.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("How much one press of a button changes the number.")
    float step = 1.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Digits shown after the decimal point.")
    DEKI_RANGE(0, 4)
    int32_t decimals = 0;

    DEKI_EXPORT
    DEKI_TOOLTIP("Past the highest number comes the lowest, and the other way round.")
    bool wrap = false;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled spinbox ignores presses and keys.")
    bool isEnabled = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the spinbox, buttons included, in meters.")
    DEKI_UNIT(Distance)
    float width = 6.5f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the spinbox in meters. The buttons are square.")
    DEKI_UNIT(Distance)
    float height = 1.75f;

    // Runtime-only listeners
    std::vector<std::function<void(float)>> onValueChanged;

    SpinboxComponent() = default;
    ~SpinboxComponent() override;

    /// Sets the number, kept in range (or wrapped round, with Wrap on).
    void SetValue(float newValue);

    /// Steps the number by `steps` presses, and announces it.
    void StepBy(int32_t steps);

    void AddOnValueChangedCallback(const std::function<void(float)>& callback);

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
    Deki::Object* m_Field = nullptr;
    Deki::Object* m_Value = nullptr;
    Deki::Object* m_Minus = nullptr;
    Deki::Object* m_MinusLabel = nullptr;
    Deki::Object* m_Plus = nullptr;
    Deki::Object* m_PlusLabel = nullptr;
    Deki::Object* m_Focus = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    bool m_Focused = false;

    // The button held down: -1 minus, +1 plus, 0 none. It repeats after a
    // pause, while held, and shows lighter.
    int32_t m_Held = 0;
    float m_HeldFor = 0.0f;
    float m_NextRepeat = 0.0f;
    Deki::Color m_HeldColor;

    void EnsureParts();
    void LayoutParts();
    void Press(int32_t direction);
    void Release();
};

}  // namespace Deki2D
