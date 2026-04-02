#include "ButtonStyleComponent.h"
#include "ButtonComponent.h"
#include "SpriteComponent.h"
#include "DekiObject.h"
#include "DekiLogSystem.h"

ButtonStyleComponent::ButtonStyleComponent()
    : transition(ButtonStyleMode::ColorTint),
      normal_color(deki::Color::White),
      hovered_color(200, 200, 200),
      pressed_color(160, 160, 160),
      disabled_color(128, 128, 128, 128)
{
}

void ButtonStyleComponent::Start()
{
    SpriteComponent* spr = sprite.Get();
    if (!spr)
    {
        DEKI_LOG_WARNING("ButtonStyleComponent: No SpriteComponent referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

    ButtonComponent* btn = button.Get();
    if (!btn)
    {
        DEKI_LOG_WARNING("ButtonStyleComponent: No ButtonComponent referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

    // Register for state changes
    btn->AddOnStateChangedCallback([this](ButtonState newState) {
        ApplyState(newState);
    });

    // Apply initial state
    ApplyState(btn->GetState());
}

void ButtonStyleComponent::ApplyState(ButtonState state)
{
    SpriteComponent* spr = sprite.Get();
    if (!spr)
        return;

    switch (transition)
    {
    case ButtonStyleMode::ColorTint:
    {
        switch (state)
        {
        case ButtonState::Normal:   spr->SetTint(normal_color);   break;
        case ButtonState::Hovered:  spr->SetTint(hovered_color);  break;
        case ButtonState::Pressed:  spr->SetTint(pressed_color);  break;
        case ButtonState::Disabled: spr->SetTint(disabled_color); break;
        }
        break;
    }
    case ButtonStyleMode::SpriteSwap:
    {
        Deki::AssetRef<Sprite>* target = nullptr;
        switch (state)
        {
        case ButtonState::Normal:   target = &normal_sprite;   break;
        case ButtonState::Hovered:  target = &hovered_sprite;  break;
        case ButtonState::Pressed:  target = &pressed_sprite;  break;
        case ButtonState::Disabled: target = &disabled_sprite; break;
        }

        // Fall back to normal_sprite if the state's sprite has no GUID
        if (target && !target->HasGuid())
            target = &normal_sprite;

        if (target)
        {
            spr->sprite.guid = target->guid;
            spr->sprite.source = target->source;
            spr->sprite.ptr = target->ptr;
            spr->sprite.loadAttempted = target->loadAttempted;
        }
        break;
    }
    }
}
