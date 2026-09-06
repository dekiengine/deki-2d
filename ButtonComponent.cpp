#include "ButtonComponent.h"
#include "deki-input/InputCollider.h"
#include <deki/Object.h>
#include <deki/LogSystem.h>

ButtonComponent::ButtonComponent()
    : state(ButtonState::Normal),
      isEnabled(true),
      m_WasPressedInside(false)
{
}

ButtonComponent::~ButtonComponent()
{
}

void ButtonComponent::Start()
{
    InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        DEKI_LOG_WARNING("ButtonComponent: No InputCollider referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

    // Register pointer callbacks on InputCollider
    collider->onPointerDown.push_back([this](float x, float y) {
        (void)x; (void)y;
        if (!isEnabled) return;
        m_WasPressedInside = true;
        SetState(ButtonState::Pressed);
        InvokeCallbacks(onPress);
    });

    collider->onPointerUp.push_back([this](float x, float y) {
        (void)x; (void)y;
        if (!isEnabled) return;
        if (m_WasPressedInside)
        {
            InvokeCallbacks(onRelease);

            // Click = was pressed inside and released inside collider
            InputCollider* col = inputCollider.Get();
            if (col && col->IsPointerInside())
            {
                SetState(ButtonState::Normal);
                InvokeCallbacks(onClick);
            }
            else
            {
                SetState(ButtonState::Normal);
            }
            m_WasPressedInside = false;
        }
    });

    collider->onPointerEnter.push_back([this](float x, float y) {
        (void)x; (void)y;
        if (!isEnabled) return;
        if (!m_WasPressedInside)
        {
            SetState(ButtonState::Hovered);
        }
    });

    collider->onPointerExit.push_back([this](float x, float y) {
        (void)x; (void)y;
        if (!isEnabled) return;
        if (m_WasPressedInside)
        {
            SetState(ButtonState::Normal);
        }
        else if (state == ButtonState::Hovered)
        {
            SetState(ButtonState::Normal);
        }
    });
}

void ButtonComponent::SetState(ButtonState new_state)
{
    if (state == new_state)
        return;

    ButtonState old_state = state;
    state = new_state;

    // Trigger hover callbacks
    if (old_state != ButtonState::Hovered && new_state == ButtonState::Hovered)
    {
        InvokeCallbacks(onHoverEnter);
    }
    else if (old_state == ButtonState::Hovered && new_state != ButtonState::Hovered)
    {
        InvokeCallbacks(onHoverExit);
    }

    // Notify state change listeners
    for (const auto& cb : on_state_changed)
    {
        if (cb) cb(new_state);
    }
}

void ButtonComponent::SetEnabled(bool enabled)
{
    isEnabled = enabled;
    if (!enabled)
    {
        SetState(ButtonState::Disabled);
        m_WasPressedInside = false;
    }
    else if (state == ButtonState::Disabled)
    {
        SetState(ButtonState::Normal);
    }
}


void ButtonComponent::AddOnClickCallback(const ButtonCallback& callback)
{
    onClick.push_back(callback);
}

void ButtonComponent::AddOnPressCallback(const ButtonCallback& callback)
{
    onPress.push_back(callback);
}

void ButtonComponent::AddOnReleaseCallback(const ButtonCallback& callback)
{
    onRelease.push_back(callback);
}

void ButtonComponent::AddOnHoverEnterCallback(const ButtonCallback& callback)
{
    onHoverEnter.push_back(callback);
}

void ButtonComponent::AddOnHoverExitCallback(const ButtonCallback& callback)
{
    onHoverExit.push_back(callback);
}

void ButtonComponent::AddOnStateChangedCallback(const std::function<void(ButtonState)>& callback)
{
    on_state_changed.push_back(callback);
}

void ButtonComponent::CancelPress()
{
    if (m_WasPressedInside)
    {
        m_WasPressedInside = false;
        SetState(ButtonState::Normal);
        InvokeCallbacks(onRelease);
    }
}

void ButtonComponent::InvokeCallbacks(const std::vector<ButtonCallback>& callbacks)
{
    for (const auto& cb : callbacks)
    {
        if (cb) cb();
    }
}
