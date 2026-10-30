#include "ButtonComponent.h"
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "WidgetParts.h"
#include <deki/Object.h>
#include <deki/LogSystem.h>

namespace Deki2D
{

ButtonComponent::ButtonComponent()
    : state(ButtonState::Normal),
      isEnabled(true),
      m_WasPressedInside(false)
{
}

ButtonComponent::~ButtonComponent()
{
    FocusSystem::Unregister(this);
}

void ButtonComponent::Start()
{
    FocusSystem::Register(this);

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        DEKI_LOG_WARNING("ButtonComponent: No DekiInput::InputCollider referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

    collider->onPointerDown.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            if (!isEnabled)
            {
                return;
            }
            m_WasPressedInside = true;
            SetState(ButtonState::Pressed);
            InvokeCallbacks(onPress);
        });

    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            if (!isEnabled)
            {
                return;
            }
            if (m_WasPressedInside)
            {
                InvokeCallbacks(onRelease);

                // A click is a press and a release both inside the collider
                DekiInput::InputCollider* col = inputCollider.Get();
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

    collider->onPointerEnter.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            if (!isEnabled)
            {
                return;
            }
            if (!m_WasPressedInside)
            {
                SetState(ButtonState::Hovered);
            }
        });

    collider->onPointerExit.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            if (!isEnabled)
            {
                return;
            }
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

void ButtonComponent::SetState(ButtonState newState)
{
    if (state == newState)
    {
        return;
    }

    ButtonState oldState = state;
    state = newState;

    if (oldState != ButtonState::Hovered && newState == ButtonState::Hovered)
    {
        InvokeCallbacks(onHoverEnter);
    }
    else if (oldState == ButtonState::Hovered && newState != ButtonState::Hovered)
    {
        InvokeCallbacks(onHoverExit);
    }

    for (const auto& cb : onStateChanged)
    {
        if (cb)
        {
            cb(newState);
        }
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
    onStateChanged.push_back(callback);
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

void ButtonComponent::Click()
{
    if (!isEnabled)
    {
        return;
    }
    const ButtonState rest = state == ButtonState::Hovered ? ButtonState::Hovered : ButtonState::Normal;
    SetState(ButtonState::Pressed);
    InvokeCallbacks(onPress);
    InvokeCallbacks(onRelease);
    SetState(rest);
    InvokeCallbacks(onClick);
}

void ButtonComponent::GetFocusSize(float& w, float& h) const
{
    // The hit area is what the user sees as the button.
    const DekiInput::InputCollider* collider = inputCollider.Get();
    w = collider ? collider->width : 0.0f;
    h = collider ? collider->height : 0.0f;
}

void ButtonComponent::OnFocusChanged(bool focused)
{
    // Focus looks like hover, so a style made for the mouse shows it too. A
    // "Focus" child, when the button has one, shows as well.
    WidgetParts::ShowFocus(GetOwner(), focused);
    if (!isEnabled)
    {
        return;
    }
    if (focused && state == ButtonState::Normal)
    {
        SetState(ButtonState::Hovered);
    }
    else if (!focused && state == ButtonState::Hovered)
    {
        SetState(ButtonState::Normal);
    }
}

bool ButtonComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    if (key == DekiInput::Keys::Enter || key == DekiInput::Keys::Space)
    {
        Click();
        return true;
    }
    return false;
}

void ButtonComponent::InvokeCallbacks(const std::vector<ButtonCallback>& callbacks)
{
    for (const auto& cb : callbacks)
    {
        if (cb)
        {
            cb();
        }
    }
}

}  // namespace Deki2D
