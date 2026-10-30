#include "SpinnerComponent.h"

#include <deki/Object.h>
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"

#include <cmath>

namespace Deki2D
{

void SpinnerComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void SpinnerComponent::RuntimeUpdate(float deltaTime)
{
    SyncParts();
    if (!isSpinning || !m_Arc)
    {
        return;
    }
    // Clockwise is a falling angle.
    m_Angle = std::fmod(m_Angle - turnsPerSecond * kTwoPi * deltaTime, kTwoPi);
    m_Arc->SetRotation(m_Angle);
}

void SpinnerComponent::SyncParts()
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
        m_LastLayout = 0;
    }
    DekiRendering::Fingerprint f;
    f.Add(size);
    f.Add(thickness);
    f.Add(arcLength);
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void SpinnerComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Track = WidgetParts::ShapePart(owner, "Track", ShapeKind::Arc, WidgetColors::Track);
    m_Arc = WidgetParts::ShapePart(owner, "Arc", ShapeKind::Arc, WidgetColors::Accent);
    if (ShapeComponent* arc = WidgetParts::Shape(m_Arc))
    {
        arc->roundCaps = true;
    }
}

void SpinnerComponent::LayoutParts()
{
    WidgetParts::PlaceShape(m_Track, 0.0f, 0.0f, size, size);
    if (ShapeComponent* track = WidgetParts::Shape(m_Track))
    {
        track->SetArc(0.0f, kTwoPi);
        track->lineWidth = thickness;
    }
    WidgetParts::PlaceShape(m_Arc, 0.0f, 0.0f, size, size);
    if (ShapeComponent* arc = WidgetParts::Shape(m_Arc))
    {
        // Centred on up, so the turn is the only thing that moves it.
        const float up = kTwoPi * 0.25f;
        arc->SetArc(up - arcLength * 0.5f, up + arcLength * 0.5f);
        arc->lineWidth = thickness;
    }
}

}  // namespace Deki2D
