#include "SpinboxComponent.h"

#include <deki/Object.h>
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"
#include "WidgetText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Deki2D
{

namespace
{

// A held button steps once, waits this long, then repeats this often.
constexpr float kSpinRepeatDelay = 0.45f;
constexpr float kSpinRepeatEvery = 0.08f;

}  // namespace

SpinboxComponent::~SpinboxComponent()
{
    FocusSystem::Unregister(this);
}

void SpinboxComponent::Start()
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
            (void)y;
            const Deki::Object* owner = GetOwner();
            if (!isEnabled || !owner || FocusSystem::IsInputBlocked(owner))
            {
                return;
            }
            const float local = x - owner->GetWorldX();
            const float button = height;
            if (local < -width * 0.5f + button)
            {
                Press(-1);
            }
            else if (local > width * 0.5f - button)
            {
                Press(1);
            }
        });
    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            Release();
        });
}

void SpinboxComponent::SetValue(float newValue)
{
    const float lo = std::min(minValue, maxValue);
    const float hi = std::max(minValue, maxValue);
    if (wrap && hi > lo)
    {
        // One step past an end lands on the other end.
        if (newValue > hi + 1e-4f)
        {
            newValue = lo;
        }
        else if (newValue < lo - 1e-4f)
        {
            newValue = hi;
        }
    }
    value = std::clamp(newValue, lo, hi);
}

void SpinboxComponent::StepBy(int32_t steps)
{
    const float old = value;
    SetValue(value + step * static_cast<float>(steps));
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

void SpinboxComponent::AddOnValueChangedCallback(const std::function<void(float)>& callback)
{
    onValueChanged.push_back(callback);
}

void SpinboxComponent::Press(int32_t direction)
{
    Release();
    m_Held = direction;
    m_HeldFor = 0.0f;
    m_NextRepeat = kSpinRepeatDelay;
    if (ShapeComponent* shape = WidgetParts::Shape(direction < 0 ? m_Minus : m_Plus))
    {
        m_HeldColor = shape->color;
        shape->color = WidgetParts::LerpColor(shape->color, Deki::Color(255, 255, 255, shape->color.a), 0.25f);
    }
    StepBy(direction);
}

void SpinboxComponent::Release()
{
    if (m_Held == 0)
    {
        return;
    }
    WidgetParts::TintShape(m_Held < 0 ? m_Minus : m_Plus, m_HeldColor);
    m_Held = 0;
}

void SpinboxComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void SpinboxComponent::RuntimeUpdate(float deltaTime)
{
    if (m_Held != 0)
    {
        m_HeldFor += deltaTime;
        while (m_HeldFor >= m_NextRepeat)
        {
            m_NextRepeat += kSpinRepeatEvery;
            StepBy(m_Held);
        }
    }
    SyncParts();
}

void SpinboxComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    WidgetParts::ShowFocus(GetOwner(), focused);
}

bool SpinboxComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    if (key == DekiInput::Keys::Left || key == DekiInput::Keys::Right)
    {
        StepBy(key == DekiInput::Keys::Right ? 1 : -1);
        return true;
    }
    return false;
}

void SpinboxComponent::SyncParts()
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
    f.Add(value);
    f.Add(decimals);
    f.Add(width);
    f.Add(height);
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void SpinboxComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    const float corner = height * 0.25f;
    m_Field = WidgetParts::ShapePart(owner, "Field", ShapeKind::Rectangle, WidgetColors::Panel, corner);
    m_Value = WidgetParts::TextPart(owner, "Value", WidgetColors::Text);
    m_Minus = WidgetParts::ShapePart(owner, "Minus", ShapeKind::Rectangle, WidgetColors::Track, corner);
    m_MinusLabel = WidgetParts::TextPart(owner, "MinusLabel", WidgetColors::Text);
    m_Plus = WidgetParts::ShapePart(owner, "Plus", ShapeKind::Rectangle, WidgetColors::Track, corner);
    m_PlusLabel = WidgetParts::TextPart(owner, "PlusLabel", WidgetColors::Text);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Rectangle, WidgetColors::Focus, corner * 1.5f);
    WidgetParts::SetPartText(m_MinusLabel, "-");
    WidgetParts::SetPartText(m_PlusLabel, "+");
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

void SpinboxComponent::LayoutParts()
{
    const float button = std::min(height, width * 0.5f);
    const float edge = width * 0.5f - button * 0.5f;
    WidgetParts::PlaceShape(m_Field, 0.0f, 0.0f, width, height);
    WidgetParts::PlaceShape(m_Minus, -edge, 0.0f, button, height);
    WidgetParts::PlaceShape(m_Plus, edge, 0.0f, button, height);
    WidgetParts::PlaceText(m_MinusLabel, -edge, 0.0f, button, height);
    WidgetParts::PlaceText(m_PlusLabel, edge, 0.0f, button, height);
    WidgetParts::PlaceText(m_Value, 0.0f, 0.0f, std::max(0.0f, width - button * 2.0f), height);

    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", static_cast<int>(std::clamp<int32_t>(decimals, 0, 4)), value);
    WidgetParts::SetPartText(m_Value, text);

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
