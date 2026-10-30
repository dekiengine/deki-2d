#include "ArcComponent.h"

#include <deki/Object.h>
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"

#include <algorithm>
#include <cmath>

namespace Deki2D
{

ArcComponent::~ArcComponent()
{
    FocusSystem::Unregister(this);
}

void ArcComponent::Start()
{
    SyncParts();
    FocusSystem::Register(this);

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        return;
    }
    collider->onPointerDown.push_back(
        [this](float x, float y)
        {
            if (!interactive)
            {
                return;
            }
            m_Dragging = true;
            SetValueFromInput(ValueAtPoint(x, y, value));
        });
    collider->onPointerMove.push_back(
        [this](float x, float y)
        {
            if (m_Dragging)
            {
                SetValueFromInput(ValueAtPoint(x, y, value));
            }
        });
    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            m_Dragging = false;
        });
}

void ArcComponent::SetValue(float newValue)
{
    value = std::clamp(newValue, std::min(minValue, maxValue), std::max(minValue, maxValue));
}

float ArcComponent::GetFraction() const
{
    const float range = maxValue - minValue;
    if (range == 0.0f)
    {
        return 0.0f;
    }
    return std::clamp((value - minValue) / range, 0.0f, 1.0f);
}

void ArcComponent::AddOnValueChangedCallback(const std::function<void(float)>& callback)
{
    onValueChanged.push_back(callback);
}

float ArcComponent::ValueAtPoint(float worldX, float worldY, float previous) const
{
    const Deki::Object* owner = GetOwner();
    if (!owner || sweepAngle <= 0.0f)
    {
        return previous;
    }
    const float angle = std::atan2(worldY - owner->GetWorldY(), worldX - owner->GetWorldX());
    // How far clockwise from the start, 0 to a full turn.
    float along = std::fmod(startAngle - angle, kTwoPi);
    if (along < 0.0f)
    {
        along += kTwoPi;
    }
    float fraction = along / sweepAngle;
    if (along > sweepAngle)
    {
        // In the gap: the nearer end.
        const float pastEnd = along - sweepAngle;
        const float beforeStart = kTwoPi - along;
        fraction = pastEnd < beforeStart ? 1.0f : 0.0f;
    }
    // A drag does not jump across the gap from one end to the other.
    const float range = maxValue - minValue;
    const float previousFraction = range != 0.0f ? (previous - minValue) / range : 0.0f;
    if (std::fabs(fraction - previousFraction) > 0.5f)
    {
        fraction = previousFraction > 0.5f ? 1.0f : 0.0f;
    }
    return minValue + range * fraction;
}

void ArcComponent::SetValueFromInput(float newValue)
{
    if (step > 0.0f)
    {
        newValue = minValue + std::round((newValue - minValue) / step) * step;
    }
    const float old = value;
    SetValue(newValue);
    if (value == old)
    {
        return;
    }
    SyncParts();
    for (const auto& callback : onValueChanged)
    {
        callback(value);
    }
}

void ArcComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void ArcComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void ArcComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    WidgetParts::ShowFocus(GetOwner(), focused);
}

bool ArcComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    namespace Keys = DekiInput::Keys;
    if (key != Keys::Left && key != Keys::Right)
    {
        return false;
    }
    const float amount = step > 0.0f ? step : (maxValue - minValue) / 20.0f;
    SetValueFromInput(value + (key == Keys::Right ? amount : -amount));
    return true;
}

void ArcComponent::SyncParts()
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
    f.Add(GetFraction());
    f.Add(size);
    f.Add(thickness);
    f.Add(startAngle);
    f.Add(sweepAngle);
    f.Add(static_cast<uint32_t>(interactive));
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void ArcComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Track = WidgetParts::ShapePart(owner, "Track", ShapeKind::Arc, WidgetColors::Track);
    m_Indicator = WidgetParts::ShapePart(owner, "Indicator", ShapeKind::Arc, WidgetColors::Accent);
    m_Knob = WidgetParts::ShapePart(owner, "Knob", ShapeKind::Ellipse, WidgetColors::Knob);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Ellipse, WidgetColors::Focus);
    for (Deki::Object* part : { m_Track, m_Indicator })
    {
        if (ShapeComponent* shape = WidgetParts::Shape(part))
        {
            shape->roundCaps = true;
        }
    }
    if (m_Focus)
    {
        m_Focus->SetActive(m_Focused);
    }
#ifdef DEKI_EDITOR
    if (owner && !inputCollider.GetObject())
    {
        if (!owner->GetComponent<DekiInput::InputCollider>())
        {
            owner->AddComponent<DekiInput::InputCollider>();
        }
        inputCollider.Set(owner);
    }
#endif
}

void ArcComponent::LayoutParts()
{
    // Shapes run counter-clockwise from start to end; the value runs
    // clockwise from startAngle, so each arc ends at startAngle.
    const float fraction = GetFraction();
    const float endOfValue = startAngle - sweepAngle * fraction;
    WidgetParts::PlaceShape(m_Track, 0.0f, 0.0f, size, size);
    if (ShapeComponent* track = WidgetParts::Shape(m_Track))
    {
        track->SetArc(startAngle - sweepAngle, startAngle);
        track->lineWidth = thickness;
    }
    WidgetParts::PlaceShape(m_Indicator, 0.0f, 0.0f, size, size);
    if (ShapeComponent* indicator = WidgetParts::Shape(m_Indicator))
    {
        indicator->SetArc(endOfValue, startAngle);
        indicator->lineWidth = thickness;
    }
    if (m_Indicator)
    {
        m_Indicator->SetActive(fraction > 0.0f);
    }

    // The knob rides the middle of the ring.
    const float radius = (size - thickness) * 0.5f;
    const float knob = thickness * 1.6f;
    const float x = std::cos(endOfValue) * radius;
    const float y = std::sin(endOfValue) * radius;
    WidgetParts::PlaceShape(m_Knob, x, y, knob, knob);
    if (m_Knob)
    {
        m_Knob->SetActive(interactive);
    }
    const float ring = knob * 1.5f;
    WidgetParts::PlaceShape(m_Focus, x, y, ring, ring);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Focus))
    {
        shape->lineWidth = knob * 0.12f;
    }

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (collider && inputCollider.GetObject() == GetOwner())
    {
        collider->width = size;
        collider->height = size;
    }
}

}  // namespace Deki2D
