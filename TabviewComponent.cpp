#include "TabviewComponent.h"

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

TabviewComponent::~TabviewComponent()
{
    FocusSystem::Unregister(this);
}

void TabviewComponent::Start()
{
    m_Shown = static_cast<float>(selectedTab);
    SyncParts();
    FocusSystem::Register(this);

    auto* collider = m_Bar ? m_Bar->GetComponent<DekiInput::InputCollider>() : nullptr;
    if (!collider)
    {
        return;
    }
    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)y;
            auto* col = m_Bar ? m_Bar->GetComponent<DekiInput::InputCollider>() : nullptr;
            const Deki::Object* owner = GetOwner();
            if (!isEnabled || tabs.empty() || !owner || !col || !col->IsPointerInside() || width <= 0.0f ||
                FocusSystem::IsInputBlocked(owner))
            {
                return;
            }
            const float local = x - owner->GetWorldX() + width * 0.5f;
            const int32_t count = static_cast<int32_t>(tabs.size());
            SelectTab(std::clamp(static_cast<int32_t>(local / width * count), 0, count - 1));
        });
}

void TabviewComponent::SelectTab(int32_t index, bool notify)
{
    if (tabs.empty())
    {
        return;
    }
    index = std::clamp(index, 0, static_cast<int32_t>(tabs.size()) - 1);
    if (index == selectedTab)
    {
        return;
    }
    selectedTab = index;
    SyncParts();
    if (notify)
    {
        for (const auto& callback : onTabChanged)
        {
            callback(selectedTab);
        }
    }
}

void TabviewComponent::AddOnTabChangedCallback(const std::function<void(int32_t)>& callback)
{
    onTabChanged.push_back(callback);
}

Deki::Object* TabviewComponent::GetPage(int32_t index) const
{
    return index >= 0 && index < static_cast<int32_t>(m_Pages.size()) ? m_Pages[index] : nullptr;
}

Deki::Object* TabviewComponent::GetFocusObject() const
{
    return m_Bar ? m_Bar : GetOwner();
}

void TabviewComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_Shown = static_cast<float>(selectedTab);
    m_LastLayout = 0;
    SyncParts();
}

void TabviewComponent::RuntimeUpdate(float deltaTime)
{
    m_Shown = WidgetParts::Glide(m_Shown, static_cast<float>(selectedTab), glideSpeed, deltaTime);
    SyncParts();
}

void TabviewComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    WidgetParts::ShowFocus(GetOwner(), focused);
}

bool TabviewComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    namespace Keys = DekiInput::Keys;
    if (key != Keys::Left && key != Keys::Right)
    {
        return false;
    }
    const int32_t next = selectedTab + (key == Keys::Right ? 1 : -1);
    if (next < 0 || next >= static_cast<int32_t>(tabs.size()))
    {
        return false;  // past the last tab: the focus moves on
    }
    SelectTab(next);
    return true;
}

void TabviewComponent::SyncParts()
{
    Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return;
    }
    if (owner->GetChildren().size() != m_KnownChildren || m_TabParts.size() < tabs.size())
    {
        EnsureParts();
        m_KnownChildren = owner->GetChildren().size();
        m_LastLayout = 0;
    }
    if (m_Shown < 0.0f)
    {
        m_Shown = static_cast<float>(selectedTab);
    }
    DekiRendering::Fingerprint f;
    f.Add(width);
    f.Add(height);
    f.Add(barHeight);
    f.Add(selectedTab);
    f.Add(m_Shown);
    f.Add(static_cast<uint32_t>(tabs.size()));
    for (const std::string& tab : tabs)
    {
        uint32_t h = 2166136261u;
        for (char c : tab)
        {
            h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
        }
        f.Add(h);
    }
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void TabviewComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Bar = WidgetParts::ShapePart(owner, "Bar", ShapeKind::Rectangle, WidgetColors::Panel);
    m_Indicator = WidgetParts::ShapePart(owner, "Indicator", ShapeKind::Rectangle, WidgetColors::Accent, 100.0f);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Rectangle, WidgetColors::Focus);
    if (m_Focus)
    {
        m_Focus->SetActive(m_Focused);
    }
#ifdef DEKI_EDITOR
    // The bar carries the hit area, as it is the part that takes taps.
    if (m_Bar && !m_Bar->GetComponent<DekiInput::InputCollider>())
    {
        m_Bar->AddComponent<DekiInput::InputCollider>();
    }
#endif

    m_TabParts.clear();
    m_Pages.clear();
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "Tab%d", static_cast<int>(i));
        Deki::Object* tab = WidgetParts::TextPart(owner, name, WidgetColors::DimText);
        std::snprintf(name, sizeof(name), "Page%d", static_cast<int>(i));
        Deki::Object* page = WidgetParts::Find(owner, name);
#ifdef DEKI_EDITOR
        if (!page)
        {
            page = WidgetParts::MakeChild(owner, name);
        }
#endif
        if (!tab || !page)
        {
            break;  // on the device: the tabs the scene has
        }
        m_TabParts.push_back(tab);
        m_Pages.push_back(page);
    }
}

void TabviewComponent::LayoutParts()
{
    const int32_t count = static_cast<int32_t>(m_TabParts.size());
    const float barY = height * 0.5f - barHeight * 0.5f;
    WidgetParts::PlaceShape(m_Bar, 0.0f, barY, width, barHeight);
    if (auto* collider = m_Bar ? m_Bar->GetComponent<DekiInput::InputCollider>() : nullptr)
    {
        collider->width = width;
        collider->height = barHeight;
    }
    const float tabWidth = count > 0 ? width / static_cast<float>(count) : width;
    for (int32_t i = 0; i < count; ++i)
    {
        const float x = -width * 0.5f + tabWidth * (static_cast<float>(i) + 0.5f);
        WidgetParts::PlaceText(m_TabParts[i], x, barY, tabWidth, barHeight);
        if (i < static_cast<int32_t>(tabs.size()))
        {
            WidgetParts::SetPartText(m_TabParts[i], tabs[i]);
        }
        if (TextComponent* text = WidgetParts::Text(m_TabParts[i]))
        {
            text->color = i == selectedTab ? WidgetColors::Text : WidgetColors::DimText;
        }
        // Pages fill the space under the bar, centred in it.
        m_Pages[i]->SetLocalPosition(0.0f, -barHeight * 0.5f);
        WidgetParts::SetShown(m_Pages[i], i == selectedTab);
    }

    const float line = barHeight * 0.1f;
    const float x = -width * 0.5f + tabWidth * (m_Shown + 0.5f);
    WidgetParts::PlaceShape(m_Indicator, x, barY - barHeight * 0.5f + line * 0.5f, tabWidth * 0.6f, line);

    const float ring = barHeight * 0.3f;
    WidgetParts::PlaceShape(m_Focus, 0.0f, barY, width + ring, barHeight + ring);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Focus))
    {
        shape->lineWidth = barHeight * 0.08f;
    }
}

}  // namespace Deki2D
