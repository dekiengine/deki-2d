#include "LedComponent.h"

#include <deki/Object.h>
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"

#include <algorithm>

namespace Deki2D
{

namespace
{

// What a dark light looks like: the colour at a fifth, so a red light still
// reads as red when it is off.
constexpr float kDarkShare = 0.2f;

// The glow's alpha at full brightness.
constexpr float kGlowAlpha = 0.35f;

}  // namespace

void LedComponent::SetBrightness(float value)
{
    brightness = std::clamp(value, 0.0f, 1.0f);
}

void LedComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void LedComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void LedComponent::SyncParts()
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
    f.Add(color.ToRGBA8888());
    f.Add(brightness);
    f.Add(static_cast<uint32_t>(isOn));
    f.Add(size);
    f.Add(glow);
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void LedComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Glow = WidgetParts::ShapePart(owner, "Glow", ShapeKind::Ellipse, color);
    m_Body = WidgetParts::ShapePart(owner, "Body", ShapeKind::Ellipse, color);
}

void LedComponent::LayoutParts()
{
    const float lit = isOn ? std::clamp(brightness, 0.0f, 1.0f) : 0.0f;
    const Deki::Color dark(static_cast<uint8_t>(color.r * kDarkShare), static_cast<uint8_t>(color.g * kDarkShare),
                           static_cast<uint8_t>(color.b * kDarkShare), color.a);
    WidgetParts::PlaceShape(m_Body, 0.0f, 0.0f, size, size);
    WidgetParts::TintShape(m_Body, WidgetParts::LerpColor(dark, color, lit));

    const float glowSize = size * (1.0f + glow);
    WidgetParts::PlaceShape(m_Glow, 0.0f, 0.0f, glowSize, glowSize);
    Deki::Color halo = color;
    halo.a = static_cast<uint8_t>(color.a * kGlowAlpha * lit);
    WidgetParts::TintShape(m_Glow, halo);
    if (m_Glow)
    {
        // A glow with no light behind it is not drawn at all.
        m_Glow->SetActive(halo.a > 0 && glow > 0.0f);
    }
}

}  // namespace Deki2D
