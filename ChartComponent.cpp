#include "ChartComponent.h"

#include <deki/Engine.h>
#include <deki/Object.h>
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"

#include <algorithm>
#include <cmath>

namespace Deki2D
{

namespace
{

// The series keeps this many pixels clear of the chart's edges, so a line
// at the top or bottom value is drawn whole.
constexpr float kChartInsetPx = 2.0f;

float ChartClamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Coverage of a line from a to b, `half` pixels either side, with a
// one-pixel soft edge, merged into the mask by keeping the larger value.
void DrawSegment(uint8_t* mask, int32_t w, int32_t h, float ax, float ay, float bx, float by, float half)
{
    const int32_t x0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(std::min(ax, bx) - half - 1.0f)));
    const int32_t x1 = std::min(w - 1, static_cast<int32_t>(std::ceil(std::max(ax, bx) + half + 1.0f)));
    const int32_t y0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(std::min(ay, by) - half - 1.0f)));
    const int32_t y1 = std::min(h - 1, static_cast<int32_t>(std::ceil(std::max(ay, by) + half + 1.0f)));
    const float dx = bx - ax;
    const float dy = by - ay;
    const float lengthSquared = dx * dx + dy * dy;
    for (int32_t y = y0; y <= y1; ++y)
    {
        uint8_t* row = mask + static_cast<size_t>(y) * w;
        const float py = static_cast<float>(y) + 0.5f;
        for (int32_t x = x0; x <= x1; ++x)
        {
            const float px = static_cast<float>(x) + 0.5f;
            const float t =
                lengthSquared > 0.0f ? ChartClamp01(((px - ax) * dx + (py - ay) * dy) / lengthSquared) : 0.0f;
            const float ex = px - (ax + dx * t);
            const float ey = py - (ay + dy * t);
            const float d = std::sqrt(ex * ex + ey * ey) - half;
            const uint8_t cover = static_cast<uint8_t>(ChartClamp01(0.5f - d) * 255.0f + 0.5f);
            row[x] = std::max(row[x], cover);
        }
    }
}

}  // namespace

void ChartComponent::AddPoint(float value)
{
    const size_t limit = static_cast<size_t>(std::max<int32_t>(2, pointCount));
    // Full: shift down in place, so a scrolling chart never allocates.
    if (points.size() >= limit)
    {
        std::move(points.begin() + static_cast<std::ptrdiff_t>(points.size() - limit + 1), points.end(),
                  points.begin());
        points.resize(limit - 1);
    }
    points.push_back(value);
}

void ChartComponent::SetPoint(int32_t index, float value)
{
    if (index >= 0 && index < static_cast<int32_t>(points.size()))
    {
        points[index] = value;
    }
}

void ChartComponent::Clear()
{
    points.clear();
}

void ChartComponent::Start()
{
    // Room for every point up front: AddPoint then never grows the vector.
    points.reserve(static_cast<size_t>(std::max<int32_t>(2, pointCount)));
    SyncParts();
}

void ChartComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastGrid = 0;
    m_LastSeries = 0;
    SyncParts();
}

void ChartComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void ChartComponent::SyncParts()
{
    Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return;
    }
    if (owner->GetChildren().size() != m_KnownChildren)
    {
        EnsureParts();
        m_KnownChildren = owner->GetChildren().size();
        m_LastGrid = 0;
        m_LastSeries = 0;
    }
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    DekiRendering::Fingerprint size;
    size.Add(width);
    size.Add(height);
    size.Add(ppm);
    DekiRendering::Fingerprint grid = size;
    grid.Add(gridLines);
    if (grid.Value() != m_LastGrid)
    {
        m_LastGrid = grid.Value();
        WidgetParts::PlaceShape(m_Back, 0.0f, 0.0f, width, height);
        WidgetParts::PlaceShape(m_Grid, 0.0f, 0.0f, width, height);
        BakeGrid();
    }
    DekiRendering::Fingerprint series = size;
    series.Add(static_cast<uint32_t>(kind));
    series.Add(pointCount);
    series.Add(minValue);
    series.Add(maxValue);
    series.Add(lineWidth);
    series.Add(static_cast<uint32_t>(points.size()));
    for (float value : points)
    {
        series.Add(value);
    }
    if (series.Value() != m_LastSeries)
    {
        m_LastSeries = series.Value();
        WidgetParts::PlaceShape(m_Series, 0.0f, 0.0f, width, height);
        BakeSeries();
    }
}

void ChartComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Back = WidgetParts::ShapePart(owner, "Back", ShapeKind::Rectangle, WidgetColors::Panel, 0.3f);
    m_Grid = WidgetParts::ShapePart(owner, "Grid", ShapeKind::Custom, WidgetColors::Track);
    m_Series = WidgetParts::ShapePart(owner, "Series", ShapeKind::Custom, WidgetColors::Accent);
}

void ChartComponent::BakeGrid()
{
    ShapeComponent* shape = WidgetParts::Shape(m_Grid);
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t w = static_cast<int32_t>(std::lround(width * ppm));
    const int32_t h = static_cast<int32_t>(std::lround(height * ppm));
    uint8_t* mask = shape && w > 0 && h > 0 ? shape->BeginCustomMask(w, h) : nullptr;
    if (!mask)
    {
        return;
    }
    // Lines a pixel thick between the top and the bottom, edges left out.
    for (int32_t i = 1; i <= gridLines; ++i)
    {
        const int32_t y = static_cast<int32_t>(std::lround(static_cast<float>(h) * i / (gridLines + 1)));
        if (y > 0 && y < h)
        {
            std::fill(mask + static_cast<size_t>(y) * w, mask + static_cast<size_t>(y + 1) * w, uint8_t(255));
        }
    }
    shape->EndCustomMask();
}

void ChartComponent::BakeSeries()
{
    ShapeComponent* shape = WidgetParts::Shape(m_Series);
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t w = static_cast<int32_t>(std::lround(width * ppm));
    const int32_t h = static_cast<int32_t>(std::lround(height * ppm));
    uint8_t* mask = shape && w > 0 && h > 0 ? shape->BeginCustomMask(w, h) : nullptr;
    if (!mask)
    {
        return;
    }
    const int32_t slots = std::max<int32_t>(2, pointCount);
    const int32_t count = std::min<int32_t>(static_cast<int32_t>(points.size()), slots);
    const int32_t first = static_cast<int32_t>(points.size()) - count;  // the newest `count`
    const float range = maxValue - minValue;
    const float inset = kChartInsetPx;
    const float spanX = static_cast<float>(w) - inset * 2.0f;
    const float spanY = static_cast<float>(h) - inset * 2.0f;
    // Pixel y of a value: max at the top row, min at the bottom.
    auto valueY = [&](float value)
    {
        const float t = range != 0.0f ? ChartClamp01((value - minValue) / range) : 0.0f;
        return inset + spanY * (1.0f - t);
    };

    if (kind == ChartKind::Bars)
    {
        const float slot = static_cast<float>(w) / static_cast<float>(slots);
        const float gap = std::max(1.0f, slot * 0.2f);
        for (int32_t i = 0; i < count; ++i)
        {
            const int32_t x0 = static_cast<int32_t>(std::lround(slot * i + gap * 0.5f));
            const int32_t x1 = static_cast<int32_t>(std::lround(slot * (i + 1) - gap * 0.5f));
            const int32_t top = std::clamp<int32_t>(static_cast<int32_t>(std::lround(valueY(points[first + i]))), 0, h);
            for (int32_t y = top; y < h; ++y)
            {
                std::fill(mask + static_cast<size_t>(y) * w + std::max<int32_t>(0, x0),
                          mask + static_cast<size_t>(y) * w + std::min(w, x1), uint8_t(255));
            }
        }
    }
    else
    {
        // Points spread over the chart's whole width, the newest at the right.
        const float step = slots > 1 ? spanX / static_cast<float>(slots - 1) : 0.0f;
        const float half = std::max(0.5f, lineWidth * ppm * 0.5f);
        const float startX = inset + step * static_cast<float>(slots - count);
        for (int32_t i = 0; i + 1 < count; ++i)
        {
            DrawSegment(mask, w, h, startX + step * i, valueY(points[first + i]), startX + step * (i + 1),
                        valueY(points[first + i + 1]), half);
        }
        if (count == 1)
        {
            DrawSegment(mask, w, h, startX, valueY(points[first]), startX, valueY(points[first]), half);
        }
    }
    shape->EndCustomMask();
}

}  // namespace Deki2D
