#include "SwitchComponent.h"

#include <deki/Object.h>
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"

#include <algorithm>

namespace Deki2D
{

SwitchComponent::~SwitchComponent()
{
    FocusSystem::Unregister(this);
}

void SwitchComponent::Start()
{
    m_Shown = isOn ? 1.0f : 0.0f;
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
            (void)x;
            (void)y;
            m_Pressed = isEnabled;
        });
    // A tap is a press and a release both inside, as for a button. A scroll
    // that takes the drag over cancels the press, with the pointer outside.
    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            DekiInput::InputCollider* col = inputCollider.Get();
            if (m_Pressed && isEnabled && col && col->IsPointerInside())
            {
                Toggle();
            }
            m_Pressed = false;
        });
}

void SwitchComponent::SetOn(bool on, bool notify)
{
    if (on == isOn)
    {
        return;
    }
    isOn = on;
    if (notify)
    {
        for (const ToggleCallback& callback : onToggled)
        {
            callback(isOn);
        }
    }
}

void SwitchComponent::AddOnToggledCallback(const ToggleCallback& callback)
{
    onToggled.push_back(callback);
}

void SwitchComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_Shown = isOn ? 1.0f : 0.0f;
    m_LastLayout = 0;
    SyncParts();
}

void SwitchComponent::RuntimeUpdate(float deltaTime)
{
    m_Shown = WidgetParts::Glide(m_Shown, isOn ? 1.0f : 0.0f, glideSpeed, deltaTime);
    SyncParts();
}

void SwitchComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    WidgetParts::ShowFocus(GetOwner(), focused);
}

bool SwitchComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    if (key == DekiInput::Keys::Enter || key == DekiInput::Keys::Space)
    {
        Toggle();
        return true;
    }
    return false;
}

void SwitchComponent::SyncParts()
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
    if (m_Shown < 0.0f)
    {
        m_Shown = isOn ? 1.0f : 0.0f;
    }
    DekiRendering::Fingerprint f;
    f.Add(width);
    f.Add(height);
    f.Add(m_Shown);
    f.Add(onColor.ToRGBA8888());
    f.Add(offColor.ToRGBA8888());
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void SwitchComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Track = WidgetParts::ShapePart(owner, "Track", ShapeKind::Rectangle, offColor, 100.0f);
    m_Knob = WidgetParts::ShapePart(owner, "Knob", ShapeKind::Ellipse, WidgetColors::Knob);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Rectangle, WidgetColors::Focus, 100.0f);
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

void SwitchComponent::LayoutParts()
{
    WidgetParts::PlaceShape(m_Track, 0.0f, 0.0f, width, height);
    WidgetParts::TintShape(m_Track, WidgetParts::LerpColor(offColor, onColor, m_Shown));

    // The knob sits inside the track with a small margin all round.
    const float margin = height * 0.12f;
    const float knob = std::max(0.0f, height - margin * 2.0f);
    const float travel = std::max(0.0f, width - margin * 2.0f - knob);
    const float x = -travel * 0.5f + travel * m_Shown;
    WidgetParts::PlaceShape(m_Knob, x, 0.0f, knob, knob);

    const float ring = height * 0.3f;
    WidgetParts::PlaceShape(m_Focus, 0.0f, 0.0f, width + ring, height + ring);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Focus))
    {
        shape->lineWidth = height * 0.08f;
    }

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (collider && inputCollider.GetObject() == GetOwner())
    {
        collider->width = width;
        collider->height = height;
    }
}

}  // namespace Deki2D
