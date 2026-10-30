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

/// Which way a bar or slider runs.
enum class BarOrientation : uint8_t
{
    Horizontal = 0,  // Fills left to right
    Vertical = 1     // Fills bottom to top
};

/// A bar that shows a value between a minimum and a maximum: a health bar,
/// a loading bar, a volume level.
///
/// It lays out two child parts it finds by name: Track, the whole length,
/// and Fill, the part up to the value. The editor makes them as rounded
/// ShapeComponents, which can be restyled, or swapped for sprites: the bar
/// only places and sizes them. A change of value can glide rather than jump.
///
/// Example:
///
///     auto* bar = entity->GetComponent<BarComponent>();
///     bar->SetValue(42.0f);
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A bar that shows a value between a minimum and a maximum: health, loading, volume.")
class BarComponent : public Deki::Component
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The value shown, between Min Value and Max Value.")
    float value = 50.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("The value at the empty end.")
    float minValue = 0.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("The value at the full end.")
    float maxValue = 100.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Horizontal fills left to right; vertical fills bottom to top.")
    BarOrientation orientation = BarOrientation::Horizontal;

    DEKI_EXPORT
    DEKI_TOOLTIP("Length of a horizontal bar, or thickness of a vertical one, in meters.")
    DEKI_UNIT(Distance)
    float width = 8.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Thickness of a horizontal bar, or length of a vertical one, in meters.")
    DEKI_UNIT(Distance)
    float height = 0.75f;

    DEKI_EXPORT
    DEKI_TOOLTIP("How fast the fill glides to a new value. 0 jumps straight there; 10 settles in about a third of a "
                 "second.")
    DEKI_RANGE(0, 60)
    float glideSpeed = 0.0f;

    BarComponent() = default;
    ~BarComponent() override = default;

    /// Sets the value, kept between the minimum and the maximum.
    void SetValue(float newValue);

    float GetValue() const { return value; }

    /// Where the value sits between minimum and maximum, 0 to 1.
    float GetFraction() const;

    /// Where the fill is drawn to, 0 to 1. Behind GetFraction() while it
    /// glides.
    float GetShownFraction() const { return m_Shown; }

    /// Places and sizes the parts, making any the editor is missing.
    void SyncParts();

    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void Start() override;
    void OnPropertyChanged(const char* propertyName) override;

protected:
    /// The length the fill can cover, in meters.
    float Length() const { return orientation == BarOrientation::Horizontal ? width : height; }

    /// The bar's thickness, in meters.
    float Thickness() const { return orientation == BarOrientation::Horizontal ? height : width; }

    /// Makes the parts this widget draws with, when the editor lacks them.
    virtual void EnsureParts();

    /// Places the parts for fill fraction `shown`.
    virtual void LayoutParts(float shown);

    /// A fingerprint of everything the layout reads, to skip unchanged frames.
    virtual uint64_t LayoutKey() const;

    Deki::Object* m_Track = nullptr;
    Deki::Object* m_Fill = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    float m_Shown = -1.0f;  // below 0: not placed yet, so the first layout jumps
};

}  // namespace Deki2D
