#pragma once

#include <stdint.h>

#include <deki/Component.h>
#include <deki/reflection/Property.h>

namespace Deki
{
class Object;
}

namespace Deki2D
{

/// A busy indicator: an arc that runs round a faint ring, for loading and
/// waiting. The arc turns by rotating its object, so it is never baked again
/// as it spins.
///
/// Its parts are children the editor makes: Track and Arc.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A busy indicator: an arc that runs round a ring.")
class SpinnerComponent : public Deki::Component
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("Diameter of the spinner in meters.")
    DEKI_UNIT(Distance)
    float size = 2.5f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Thickness of the ring in meters.")
    DEKI_UNIT(Distance)
    float thickness = 0.35f;

    DEKI_EXPORT
    DEKI_TOOLTIP("How much of the ring the moving arc covers.")
    DEKI_UNIT(Angle)
    float arcLength = 1.5707964f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Turns per second. Negative turns counter-clockwise.")
    DEKI_RANGE(-5, 5)
    float turnsPerSecond = 1.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Off stops it where it is.")
    bool isSpinning = true;

    SpinnerComponent() = default;
    ~SpinnerComponent() override = default;

    /// Places and sizes the parts, making any the editor is missing.
    void SyncParts();

    void Start() override { SyncParts(); }
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

private:
    Deki::Object* m_Track = nullptr;
    Deki::Object* m_Arc = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    float m_Angle = 0.0f;  // radians, the arc's turn

    void EnsureParts();
    void LayoutParts();
};

}  // namespace Deki2D
