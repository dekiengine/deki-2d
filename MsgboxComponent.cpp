#include "MsgboxComponent.h"

#include <deki/Object.h>
#include "ButtonComponent.h"
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "deki-rendering/RendererComponent.h"
#include "WidgetShapes.h"
#include "WidgetText.h"

#include <algorithm>
#include <cstdio>

namespace Deki2D
{

namespace
{

constexpr int32_t kMaxMsgboxButtons = 3;

// The dialog draws above everything else, open dropdown lists included.
constexpr int32_t kMsgboxSortingOrder = 2000;

// The backdrop covers far more than any screen, so every tap outside the
// panel lands on it.
constexpr float kBackdropSize = 4000.0f;

uint32_t HashText(const std::string& text)
{
    uint32_t h = 2166136261u;
    for (char c : text)
    {
        h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    }
    return h;
}

}  // namespace

MsgboxComponent::~MsgboxComponent()
{
    if (m_ScopeOpen && m_Box)
    {
        FocusSystem::PopScope(m_Box);
    }
}

void MsgboxComponent::Start()
{
    SyncParts();
    WireButtons();
    m_Started = true;
    if (isOpen)
    {
        isOpen = false;
        Open();
    }
}

void MsgboxComponent::WireButtons()
{
    for (size_t i = 0; i < m_Buttons.size(); ++i)
    {
        auto* button = m_Buttons[i]->GetComponent<ButtonComponent>();
        auto* shape = WidgetParts::Shape(m_Buttons[i]);
        if (!button)
        {
            continue;
        }
        const int32_t index = static_cast<int32_t>(i);
        button->AddOnClickCallback([this, index]() { Close(index); });
        if (shape)
        {
            // Lighter while hovered or focused, darker while held.
            const Deki::Color base = shape->color;
            button->AddOnStateChangedCallback(
                [shape, base](ButtonState state)
                {
                    const Deki::Color white(255, 255, 255, base.a);
                    const Deki::Color black(0, 0, 0, base.a);
                    if (state == ButtonState::Hovered)
                    {
                        shape->color = WidgetParts::LerpColor(base, white, 0.2f);
                    }
                    else if (state == ButtonState::Pressed)
                    {
                        shape->color = WidgetParts::LerpColor(base, black, 0.25f);
                    }
                    else
                    {
                        shape->color = base;
                    }
                });
        }
    }
}

void MsgboxComponent::Open()
{
    if (isOpen)
    {
        return;
    }
    isOpen = true;
    ShowBox();
    if (m_Box && m_Started)
    {
        // Someone steering with keys gets the first button focused; a touch
        // user gets no focus ring.
        const bool usingKeys = FocusSystem::GetFocused() != nullptr;
        FocusSystem::PushScope(m_Box, this);
        m_ScopeOpen = true;
        if (usingKeys && !m_Buttons.empty())
        {
            FocusSystem::SetFocus(m_Buttons[0]->GetComponent<ButtonComponent>());
        }
    }
}

void MsgboxComponent::Close(int32_t button)
{
    if (!isOpen)
    {
        return;
    }
    isOpen = false;
    if (m_ScopeOpen && m_Box)
    {
        FocusSystem::SetFocus(nullptr);
        FocusSystem::PopScope(m_Box);
        m_ScopeOpen = false;
    }
    ShowBox();
    for (const auto& callback : onClosed)
    {
        callback(button);
    }
}

void MsgboxComponent::AddOnClosedCallback(const std::function<void(int32_t)>& callback)
{
    onClosed.push_back(callback);
}

void MsgboxComponent::ShowBox()
{
    // Its buttons start when it first shows.
    WidgetParts::SetShown(m_Box, isOpen);
}

bool MsgboxComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    if (key == DekiInput::Keys::Esc && closeOnEsc && isOpen)
    {
        Close(-1);
        return true;
    }
    return false;
}

void MsgboxComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void MsgboxComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void MsgboxComponent::SyncParts()
{
    Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return;
    }
    const size_t boxChildren = m_Box ? m_Box->GetChildren().size() : static_cast<size_t>(-1);
    if (owner->GetChildren().size() != m_KnownChildren || boxChildren != m_KnownBoxChildren)
    {
        EnsureParts();
        m_KnownChildren = owner->GetChildren().size();
        m_KnownBoxChildren = m_Box ? m_Box->GetChildren().size() : static_cast<size_t>(-1);
        m_LastLayout = 0;
    }
    DekiRendering::Fingerprint f;
    f.Add(width);
    f.Add(height);
    f.Add(static_cast<uint32_t>(isOpen));
    f.Add(HashText(title));
    f.Add(HashText(message));
    f.Add(static_cast<uint32_t>(buttons.size()));
    for (const std::string& label : buttons)
    {
        f.Add(HashText(label));
    }
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void MsgboxComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Box = WidgetParts::Find(owner, "Box");
#ifdef DEKI_EDITOR
    if (!m_Box)
    {
        m_Box = WidgetParts::MakeChild(owner, "Box");
    }
#endif
    if (!m_Box)
    {
        return;
    }

    bool created = false;
    m_Backdrop = WidgetParts::ShapePart(m_Box, "Backdrop", ShapeKind::Rectangle, Deki::Color(0, 0, 0, 150));
    if (m_Backdrop && !m_Backdrop->GetComponent<DekiInput::InputCollider>())
    {
#ifdef DEKI_EDITOR
        auto* blocker = m_Backdrop->AddComponent<DekiInput::InputCollider>();
        blocker->width = kBackdropSize;
        blocker->height = kBackdropSize;
#endif
    }
    m_Panel = WidgetParts::ShapePart(m_Box, "Panel", ShapeKind::Rectangle, WidgetColors::Panel, 0.6f);
    m_Title = WidgetParts::TextPart(m_Box, "Title", WidgetColors::Text);
    m_Message = WidgetParts::TextPart(m_Box, "Message", WidgetColors::DimText);

    m_Buttons.clear();
    for (int32_t i = 0; i < kMaxMsgboxButtons; ++i)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "Button%d", static_cast<int>(i));
        const Deki::Color color = i == 0 ? WidgetColors::Accent : WidgetColors::Track;
        Deki::Object* button = WidgetParts::FindOrCreate<ShapeComponent>(m_Box, name, created);
        if (!button)
        {
            break;  // on the device: the buttons the scene has
        }
        if (created)
        {
            if (ShapeComponent* shape = WidgetParts::Shape(button))
            {
                shape->color = color;
                shape->cornerRadius = 0.4f;
            }
            auto* collider = button->AddComponent<DekiInput::InputCollider>();
            (void)collider;
            auto* press = button->AddComponent<ButtonComponent>();
            press->inputCollider.Set(button);
        }
        WidgetParts::TextPart(button, "Label", WidgetColors::Text);
        Deki::Object* ring = WidgetParts::ShapePart(button, "Focus", ShapeKind::Rectangle, WidgetColors::Focus, 0.5f);
        if (ring && created)
        {
            ring->SetActive(false);
        }
        m_Buttons.push_back(button);
    }
    for (Deki::Object* part : m_Box->GetChildren())
    {
        if (auto* renderer = part->GetComponent<DekiRendering::RendererComponent>())
        {
            renderer->sortingOrder = kMsgboxSortingOrder;
            renderer->ignoreClip = true;
        }
    }
    ShowBox();
}

void MsgboxComponent::LayoutParts()
{
    if (!m_Box)
    {
        return;
    }
    ShowBox();
    WidgetParts::PlaceShape(m_Backdrop, 0.0f, 0.0f, kBackdropSize, kBackdropSize);
    WidgetParts::PlaceShape(m_Panel, 0.0f, 0.0f, width, height);

    const float pad = 0.6f;
    const float titleHeight = 1.6f;
    const float buttonHeight = 1.75f;
    const float inner = std::max(0.0f, width - pad * 2.0f);
    WidgetParts::PlaceText(m_Title, 0.0f, height * 0.5f - pad - titleHeight * 0.5f, inner, titleHeight);
    WidgetParts::SetPartText(m_Title, title);

    const float messageTop = height * 0.5f - pad - titleHeight;
    const float messageBottom = -height * 0.5f + pad * 2.0f + buttonHeight;
    const float messageHeight = std::max(0.0f, messageTop - messageBottom);
    WidgetParts::PlaceText(m_Message, 0.0f, (messageTop + messageBottom) * 0.5f, inner, messageHeight);
    WidgetParts::SetPartText(m_Message, message);

    const int32_t count =
        std::min<int32_t>(static_cast<int32_t>(buttons.size()), static_cast<int32_t>(m_Buttons.size()));
    const float buttonWidth =
        count > 0 ? (inner - pad * static_cast<float>(count - 1)) / static_cast<float>(count) : 0.0f;
    const float buttonY = -height * 0.5f + pad + buttonHeight * 0.5f;
    for (int32_t i = 0; i < static_cast<int32_t>(m_Buttons.size()); ++i)
    {
        Deki::Object* button = m_Buttons[i];
        button->SetActive(i < count);
        if (i >= count)
        {
            continue;
        }
        const float x = -inner * 0.5f + buttonWidth * 0.5f + (buttonWidth + pad) * static_cast<float>(i);
        WidgetParts::PlaceShape(button, x, buttonY, buttonWidth, buttonHeight);
        if (auto* collider = button->GetComponent<DekiInput::InputCollider>())
        {
            collider->width = buttonWidth;
            collider->height = buttonHeight;
        }
        Deki::Object* label = WidgetParts::Find(button, "Label");
        WidgetParts::PlaceText(label, 0.0f, 0.0f, buttonWidth, buttonHeight);
        WidgetParts::SetPartText(label, buttons[i]);
        Deki::Object* ring = WidgetParts::Find(button, "Focus");
        WidgetParts::PlaceShape(ring, 0.0f, 0.0f, buttonWidth + 0.45f, buttonHeight + 0.45f);
        if (ShapeComponent* shape = WidgetParts::Shape(ring))
        {
            shape->lineWidth = 0.14f;
        }
    }
}

}  // namespace Deki2D
