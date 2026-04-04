#include "ButtonComponent.h"
#include "deki-input/InputCollider.h"
#include "DekiObject.h"
#include "DekiLogSystem.h"

ButtonComponent::ButtonComponent()
    : state(ButtonState::Normal),
      is_enabled(true),
      m_WasPressedInside(false)
{
}

ButtonComponent::~ButtonComponent()
{
}

void ButtonComponent::Start()
{
    InputCollider* collider = input_collider.Get();
    if (!collider)
    {
        DEKI_LOG_WARNING("ButtonComponent: No InputCollider referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

    // Register pointer callbacks on InputCollider
    collider->on_pointer_down.push_back([this](int32_t x, int32_t y) {
        (void)x; (void)y;
        if (!is_enabled) return;
        m_WasPressedInside = true;
        SetState(ButtonState::Pressed);
        InvokeCallbacks(on_press);
    });

    collider->on_pointer_up.push_back([this](int32_t x, int32_t y) {
        (void)x; (void)y;
        if (!is_enabled) return;
        if (m_WasPressedInside)
        {
            InvokeCallbacks(on_release);

            // Click = was pressed inside and pointer is still inside collider
            InputCollider* col = input_collider.Get();
            if (col && col->IsPointerInside())
            {
                SetState(ButtonState::Hovered);
                InvokeCallbacks(on_click);
            }
            else
            {
                SetState(ButtonState::Normal);
            }
            m_WasPressedInside = false;
        }
    });

    collider->on_pointer_enter.push_back([this](int32_t x, int32_t y) {
        (void)x; (void)y;
        if (!is_enabled) return;
        if (!m_WasPressedInside)
        {
            SetState(ButtonState::Hovered);
        }
    });

    collider->on_pointer_exit.push_back([this](int32_t x, int32_t y) {
        (void)x; (void)y;
        if (!is_enabled) return;
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
        InvokeCallbacks(on_hover_enter);
    }
    else if (old_state == ButtonState::Hovered && new_state != ButtonState::Hovered)
    {
        InvokeCallbacks(on_hover_exit);
    }

    // Notify state change listeners
    for (const auto& cb : on_state_changed)
    {
        if (cb) cb(new_state);
    }
}

void ButtonComponent::SetEnabled(bool enabled)
{
    is_enabled = enabled;
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
    on_click.push_back(callback);
}

void ButtonComponent::AddOnPressCallback(const ButtonCallback& callback)
{
    on_press.push_back(callback);
}

void ButtonComponent::AddOnReleaseCallback(const ButtonCallback& callback)
{
    on_release.push_back(callback);
}

void ButtonComponent::AddOnHoverEnterCallback(const ButtonCallback& callback)
{
    on_hover_enter.push_back(callback);
}

void ButtonComponent::AddOnHoverExitCallback(const ButtonCallback& callback)
{
    on_hover_exit.push_back(callback);
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
        InvokeCallbacks(on_release);
    }
}

void ButtonComponent::InvokeCallbacks(const std::vector<ButtonCallback>& callbacks)
{
    for (const auto& cb : callbacks)
    {
        if (cb) cb();
    }
}
