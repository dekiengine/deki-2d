#include "CheckboxComponent.h"

#include <deki/Object.h>
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"
#include "WidgetText.h"

#include <algorithm>
#include <cstring>

namespace Deki2D
{

CheckboxComponent::~CheckboxComponent()
{
    FocusSystem::Unregister(this);
}

void CheckboxComponent::Start()
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
            (void)x;
            (void)y;
            m_Pressed = isEnabled;
        });
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

void CheckboxComponent::SetChecked(bool checked, bool notify)
{
    if (checked == isChecked)
    {
        return;
    }
    isChecked = checked;
    SyncParts();
    if (notify)
    {
        for (const CheckCallback& callback : onToggled)
        {
            callback(isChecked);
        }
    }
}

void CheckboxComponent::AddOnToggledCallback(const CheckCallback& callback)
{
    onToggled.push_back(callback);
}

void CheckboxComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void CheckboxComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void CheckboxComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    WidgetParts::ShowFocus(GetOwner(), focused);
}

bool CheckboxComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    if (key == DekiInput::Keys::Enter || key == DekiInput::Keys::Space)
    {
        Toggle();
        return true;
    }
    return false;
}

void CheckboxComponent::SyncParts()
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
    f.Add(width);
    f.Add(boxSize);
    f.Add(static_cast<uint32_t>(isChecked));
    f.Add(checkedColor.ToRGBA8888());
    f.Add(uncheckedColor.ToRGBA8888());
    // The label by content, not by address: the string can change in place.
    for (size_t i = 0; i < label.size(); i += 4)
    {
        uint32_t word = 0;
        std::memcpy(&word, label.data() + i, std::min<size_t>(4, label.size() - i));
        f.Add(word);
    }
    f.Add(static_cast<uint32_t>(label.size()));
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void CheckboxComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Box = WidgetParts::ShapePart(owner, "Box", ShapeKind::Rectangle, uncheckedColor, boxSize * 0.2f);
    m_Check = WidgetParts::ShapePart(owner, "Check", ShapeKind::Check, WidgetColors::Knob);
    m_Label = WidgetParts::TextPart(owner, "Label", WidgetColors::Text, TextAlign::Left);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Rectangle, WidgetColors::Focus, boxSize * 0.3f);
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

void CheckboxComponent::LayoutParts()
{
    // The box at the left end, the label filling the rest.
    const float boxX = -width * 0.5f + boxSize * 0.5f;
    WidgetParts::PlaceShape(m_Box, boxX, 0.0f, boxSize, boxSize);
    if (ShapeComponent* box = WidgetParts::Shape(m_Box))
    {
        // Ticked: a filled box. Not ticked: an outline.
        box->color = isChecked ? checkedColor : uncheckedColor;
        box->lineWidth = isChecked ? 0.0f : boxSize * 0.12f;
    }
    if (m_Check)
    {
        m_Check->SetActive(isChecked);
        WidgetParts::PlaceShape(m_Check, boxX, 0.0f, boxSize * 0.8f, boxSize * 0.8f);
    }

    const float gap = boxSize * 0.4f;
    const float labelWidth = std::max(0.0f, width - boxSize - gap);
    WidgetParts::PlaceText(m_Label, boxX + boxSize * 0.5f + gap + labelWidth * 0.5f, 0.0f, labelWidth, boxSize);
    WidgetParts::SetPartText(m_Label, label);

    const float ring = boxSize * 0.35f;
    WidgetParts::PlaceShape(m_Focus, boxX, 0.0f, boxSize + ring, boxSize + ring);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Focus))
    {
        shape->lineWidth = boxSize * 0.08f;
    }

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (collider && inputCollider.GetObject() == GetOwner())
    {
        collider->width = width;
        collider->height = boxSize;
    }
}

}  // namespace Deki2D
