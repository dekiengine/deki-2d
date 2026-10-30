#include "BarComponent.h"

#include <deki/Object.h>
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"

#include <algorithm>
#include <cmath>

namespace Deki2D
{

// Sizes and positions are world meters. The bar is centred on its object.

void BarComponent::SetValue(float newValue)
{
    const float lo = std::min(minValue, maxValue);
    const float hi = std::max(minValue, maxValue);
    value = std::clamp(newValue, lo, hi);
}

float BarComponent::GetFraction() const
{
    const float range = maxValue - minValue;
    if (range == 0.0f)
    {
        return 0.0f;
    }
    return std::clamp((value - minValue) / range, 0.0f, 1.0f);
}

void BarComponent::Start()
{
    m_Shown = GetFraction();
    SyncParts();
}

void BarComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    // Edited in the inspector: show the new value at once, no glide.
    m_Shown = GetFraction();
    SyncParts();
}

void BarComponent::RuntimeUpdate(float deltaTime)
{
    const float target = GetFraction();
    if (m_Shown < 0.0f || glideSpeed <= 0.0f)
    {
        m_Shown = target;
    }
    else if (m_Shown != target)
    {
        // Eases in: the gap shrinks by the same share every second.
        m_Shown += (target - m_Shown) * std::min(1.0f, deltaTime * glideSpeed);
        if (std::fabs(target - m_Shown) < 0.0005f)
        {
            m_Shown = target;
        }
    }
    SyncParts();
}

uint64_t BarComponent::LayoutKey() const
{
    DekiRendering::Fingerprint f;
    f.Add(width);
    f.Add(height);
    f.Add(static_cast<uint32_t>(orientation));
    f.Add(m_Shown);
    return f.Value();
}

void BarComponent::SyncParts()
{
    Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return;
    }
    // Finding the parts walks the children by name, so it runs only when
    // the children change.
    if (owner->GetChildren().size() != m_KnownChildren)
    {
        EnsureParts();
        m_KnownChildren = owner->GetChildren().size();
        m_LastLayout = 0;
    }
    if (m_Shown < 0.0f)
    {
        m_Shown = GetFraction();
    }
    const uint64_t key = LayoutKey();
    if (key == m_LastLayout)
    {
        return;
    }
    m_LastLayout = key;
    LayoutParts(m_Shown);
}

void BarComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    // A corner radius past half the thickness is clamped to it: a pill.
    m_Track = WidgetParts::ShapePart(owner, "Track", ShapeKind::Rectangle, WidgetColors::Track, 100.0f);
    m_Fill = WidgetParts::ShapePart(owner, "Fill", ShapeKind::Rectangle, WidgetColors::Accent, 100.0f);
}

void BarComponent::LayoutParts(float shown)
{
    const bool horizontal = orientation == BarOrientation::Horizontal;
    WidgetParts::PlaceShape(m_Track, 0.0f, 0.0f, width, height);

    // The fill grows from the left or bottom end.
    const float length = Length() * shown;
    const float centre = -Length() * 0.5f + length * 0.5f;
    if (horizontal)
    {
        WidgetParts::PlaceShape(m_Fill, centre, 0.0f, length, height);
    }
    else
    {
        WidgetParts::PlaceShape(m_Fill, 0.0f, centre, width, length);
    }
}

}  // namespace Deki2D
