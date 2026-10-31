#pragma once

#include <stdint.h>
#include <vector>

#include <deki/Component.h>
#include <deki/reflection/Property.h>

namespace Deki
{
class Object;
}

namespace Deki2D
{

/// How a chart draws its points.
enum class ChartKind : uint8_t
{
    Line = 0,  // Points joined by a line
    Bars = 1   // A bar per point
};

/// A chart of a series of values: a frame rate, a temperature, a score over
/// time. AddPoint pushes a value on at the right and, once Point Count is
/// reached, the oldest off at the left, so it scrolls like a monitor.
///
/// The series and the grid are each one mask, baked again only when a value
/// changes. Its parts are children the editor makes: Back, Grid and Series;
/// the series' colour is the Series part's.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A line or bar chart of a series of values.")
class ChartComponent : public Deki::Component
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("A line through the points, or a bar for each.")
    ChartKind kind = ChartKind::Line;

    DEKI_EXPORT
    DEKI_TOOLTIP("The values, oldest first. AddPoint adds to the end.")
    std::vector<float> points = { 20.0f, 45.0f, 30.0f, 60.0f, 55.0f, 80.0f, 70.0f };

    DEKI_EXPORT
    DEKI_TOOLTIP("How many points the chart holds. Past this, adding a point drops the oldest.")
    DEKI_RANGE(2, 256)
    int32_t pointCount = 16;

    DEKI_EXPORT
    DEKI_TOOLTIP("The value at the bottom edge.")
    float minValue = 0.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("The value at the top edge.")
    float maxValue = 100.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Horizontal grid lines across the chart. 0 for none.")
    DEKI_RANGE(0, 16)
    int32_t gridLines = 4;

    DEKI_EXPORT
    DEKI_TOOLTIP("Thickness of the line in meters.")
    DEKI_UNIT(Distance)
    float lineWidth = 0.18f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the chart in meters.")
    DEKI_UNIT(Distance)
    float width = 12.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the chart in meters.")
    DEKI_UNIT(Distance)
    float height = 6.0f;

    ChartComponent() = default;
    ~ChartComponent() override = default;

    /// Adds a value at the right, dropping the oldest once the chart is full.
    void AddPoint(float value);

    /// Sets point `index` (from the oldest), when there is one.
    void SetPoint(int32_t index, float value);

    void Clear();

    /// Places and sizes the parts and bakes the masks, making any part the
    /// editor is missing.
    void SyncParts();

    void Start() override;
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

private:
    Deki::Object* m_Back = nullptr;
    Deki::Object* m_Grid = nullptr;
    Deki::Object* m_Series = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastGrid = 0;
    uint64_t m_LastSeries = 0;

    void EnsureParts();
    void BakeGrid();
    void BakeSeries();
};

}  // namespace Deki2D
