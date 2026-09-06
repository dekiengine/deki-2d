#include "ButtonStyleComponent.h"
#include "ButtonComponent.h"
#include "SpriteComponent.h"
#include <deki/Object.h>
#include <deki/LogSystem.h>

ButtonStyleComponent::ButtonStyleComponent()
    : transition(ButtonStyleMode::ColorTint),
      normalColor(Deki::Color::White),
      hoveredColor(200, 200, 200),
      pressedColor(160, 160, 160),
      disabledColor(128, 128, 128, 128)
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
        case ButtonState::Normal:   spr->SetTint(normalColor);   break;
        case ButtonState::Hovered:  spr->SetTint(hoveredColor);  break;
        case ButtonState::Pressed:  spr->SetTint(pressedColor);  break;
        case ButtonState::Disabled: spr->SetTint(disabledColor); break;
        }
        break;
    }
    case ButtonStyleMode::SpriteSwap:
    {
        Deki::AssetRef<Sprite>* target = nullptr;
        switch (state)
        {
        case ButtonState::Normal:   target = &normalSprite;   break;
        case ButtonState::Hovered:  target = &hoveredSprite;  break;
        case ButtonState::Pressed:  target = &pressedSprite;  break;
        case ButtonState::Disabled: target = &disabledSprite; break;
        }

        // Fall back to normalSprite if the state's sprite has no GUID
        if (target && !target->HasGuid())
            target = &normalSprite;

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
