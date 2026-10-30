#pragma once

#include <stdint.h>

#include <deki/Color.h>
#include <deki/Component.h>
#include <deki/reflection/Property.h>

namespace Deki
{
class Object;
}

namespace Deki2D
{

/// A round light: a status dot, a record lamp, a power light. It shines in
/// its colour at a brightness from 0 (dark) to 1, with a soft glow round it
/// when lit.
///
/// Its parts are children the editor makes: Glow and Body.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A round light with a colour, a brightness and a glow.")
class LedComponent : public Deki::Component
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The colour of the light at full brightness.")
    Deki::Color color = Deki::Color(64, 220, 120);

    DEKI_EXPORT
    DEKI_TOOLTIP("How bright the light is, 0 (dark) to 1.")
    DEKI_SLIDER(0, 1)
    float brightness = 1.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Off shows the light dark, whatever the brightness.")
    bool isOn = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Diameter of the light in meters.")
    DEKI_UNIT(Distance)
    float size = 1.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("How far the glow reaches past the light, as a share of its size. 0 for none.")
    DEKI_RANGE(0, 2)
    float glow = 0.6f;

    LedComponent() = default;
    ~LedComponent() override = default;

    void SetOn(bool on) { isOn = on; }
    void Toggle() { isOn = !isOn; }

    /// Sets the brightness, kept between 0 and 1.
    void SetBrightness(float value);

    /// Places, sizes and colours the parts, making any the editor is missing.
    void SyncParts();

    void Start() override { SyncParts(); }
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

private:
    Deki::Object* m_Glow = nullptr;
    Deki::Object* m_Body = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;

    void EnsureParts();
    void LayoutParts();
};

}  // namespace Deki2D
