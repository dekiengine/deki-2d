#include "SliderComponent.h"

#include <deki/Object.h>
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"

#include <algorithm>
#include <cmath>

namespace Deki2D
{

SliderComponent::~SliderComponent()
{
    FocusSystem::Unregister(this);
}

void SliderComponent::Start()
{
    BarComponent::Start();
    FocusSystem::Register(this);

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        return;
    }
    collider->onPointerDown.push_back(
        [this](float x, float y)
        {
            if (!isEnabled || FocusSystem::IsInputBlocked(GetOwner()))
            {
                return;
            }
            m_Dragging = true;
            SetValueFromInput(ValueAt(x, y));
        });
    collider->onPointerMove.push_back(
        [this](float x, float y)
        {
            if (m_Dragging)
            {
                SetValueFromInput(ValueAt(x, y));
            }
        });
    // Also how a scroll that takes over the drag cancels it.
    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            if (m_Dragging)
            {
                m_Dragging = false;
                Announce(true);
            }
        });
}

void SliderComponent::AddOnValueChangedCallback(const ValueCallback& callback)
{
    onValueChanged.push_back(callback);
}

void SliderComponent::AddOnValueCommittedCallback(const ValueCallback& callback)
{
    onValueCommitted.push_back(callback);
}

float SliderComponent::Snap(float v) const
{
    const float lo = std::min(minValue, maxValue);
    const float hi = std::max(minValue, maxValue);
    if (step > 0.0f)
    {
        v = minValue + std::round((v - minValue) / step) * step;
    }
    return std::clamp(v, lo, hi);
}

float SliderComponent::ValueAt(float worldX, float worldY) const
{
    const Deki::Object* owner = GetOwner();
    if (!owner || Length() <= 0.0f)
    {
        return value;
    }
    const bool horizontal = orientation == BarOrientation::Horizontal;
    const float scale = horizontal ? owner->GetWorldScaleX() : owner->GetWorldScaleY();
    const float local =
        (horizontal ? worldX - owner->GetWorldX() : worldY - owner->GetWorldY()) / (scale != 0.0f ? scale : 1.0f);
    const float fraction = std::clamp(local / Length() + 0.5f, 0.0f, 1.0f);
    return minValue + (maxValue - minValue) * fraction;
}

void SliderComponent::SetValueFromInput(float newValue)
{
    const float snapped = Snap(newValue);
    if (snapped == value)
    {
        return;
    }
    value = snapped;
    // A finger or key expects the knob under it now, not gliding behind.
    m_Shown = GetFraction();
    SyncParts();
    for (const ValueCallback& callback : onValueChanged)
    {
        callback(value);
    }
}

void SliderComponent::Announce(bool committed)
{
    if (!committed)
    {
        return;
    }
    for (const ValueCallback& callback : onValueCommitted)
    {
        callback(value);
    }
}

void SliderComponent::GetFocusSize(float& w, float& h) const
{
    const bool horizontal = orientation == BarOrientation::Horizontal;
    w = horizontal ? width + knobSize : std::max(width, knobSize);
    h = horizontal ? std::max(height, knobSize) : height + knobSize;
}

void SliderComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    WidgetParts::ShowFocus(GetOwner(), focused);
}

bool SliderComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    namespace Keys = DekiInput::Keys;
    const bool horizontal = orientation == BarOrientation::Horizontal;
    const uint32_t down = horizontal ? Keys::Left : Keys::Down;
    const uint32_t up = horizontal ? Keys::Right : Keys::Up;
    if (key != down && key != up)
    {
        return false;
    }
    float amount = keyStep;
    if (amount <= 0.0f)
    {
        amount = step > 0.0f ? step : (maxValue - minValue) / 20.0f;
    }
    SetValueFromInput(value + (key == up ? amount : -amount));
    Announce(true);
    // Used even at the end of the range, so holding the key there does not
    // jump the focus to the next widget.
    return true;
}

uint64_t SliderComponent::LayoutKey() const
{
    DekiRendering::Fingerprint f;
    const uint64_t bar = BarComponent::LayoutKey();
    f.Add(static_cast<uint32_t>(bar));
    f.Add(static_cast<uint32_t>(bar >> 32));
    f.Add(knobSize);
    return f.Value();
}

void SliderComponent::EnsureParts()
{
    BarComponent::EnsureParts();
    Deki::Object* owner = GetOwner();
    m_Knob = WidgetParts::ShapePart(owner, "Knob", ShapeKind::Ellipse, WidgetColors::Knob);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Ellipse, WidgetColors::Focus);
    if (m_Focus)
    {
        m_Focus->SetActive(m_Focused);
    }
#ifdef DEKI_EDITOR
    // A new slider gets its own hit area, sized in LayoutParts.
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

void SliderComponent::LayoutParts(float shown)
{
    BarComponent::LayoutParts(shown);
    const bool horizontal = orientation == BarOrientation::Horizontal;
    const float along = -Length() * 0.5f + Length() * shown;
    const float x = horizontal ? along : 0.0f;
    const float y = horizontal ? 0.0f : along;
    WidgetParts::PlaceShape(m_Knob, x, y, knobSize, knobSize);

    const float ring = knobSize * 1.5f;
    WidgetParts::PlaceShape(m_Focus, x, y, ring, ring);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Focus))
    {
        shape->lineWidth = knobSize * 0.12f;
    }

    // The hit area covers the track and the knob at either end.
    DekiInput::InputCollider* collider = inputCollider.Get();
    if (collider && inputCollider.GetObject() == GetOwner())
    {
        GetFocusSize(collider->width, collider->height);
    }
}

}  // namespace Deki2D
