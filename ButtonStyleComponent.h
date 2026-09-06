#pragma once

#include <stdint.h>

#include <deki/Behaviour.h>
#include <deki/assets/AssetRef.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/Property.h>
#include <deki/Color.h>
#include "Sprite.h"

// Forward declarations
class ButtonComponent;
class SpriteComponent;
enum class ButtonState : uint8_t;

/**
 * @brief Transition mode for ButtonStyleComponent
 */
enum class ButtonStyleMode : uint8_t
{
    ColorTint = 0,   // Tint the existing sprite per state
    SpriteSwap = 1   // Swap entire sprite assets per state
};

/**
 * @brief CSS-like visual feedback for buttons
 *
 * ButtonStyleComponent observes a ButtonComponent via ObjectRef and modifies
 * a sibling SpriteComponent's appearance based on button state.
 *
 * Two modes:
 * - ColorTint: Changes the sprite's tint color per state
 * - SpriteSwap: Swaps the entire sprite asset per state
 *
 * Usage:
 * @code
 * auto* entity = new Deki::Object("StyledButton");
 * auto* sprite = entity->AddComponent<SpriteComponent>();
 * auto* button = entity->AddComponent<ButtonComponent>();
 * auto* style = entity->AddComponent<ButtonStyleComponent>();
 *
 * // Set button reference (required)
 * style->button.Set(entity);
 *
 * // Configure colors (ColorTint mode)
 * style->normalColor = Deki::Color::White;
 * style->pressedColor = Deki::Color(160, 160, 160);
 * @endcode
 */
class ButtonStyleComponent : public Deki::Behaviour
{
    DEKI_COMPONENT(ButtonStyleComponent, Deki::Behaviour, "2D", "0c34e973-6cd1-45d2-b2c6-f010ba320707", "DEKI_FEATURE_BUTTON")
    DEKI_DESCRIPTION("Gives a button its look per state, by tinting or by swapping sprites.")

public:

    // Button to observe (required)
    DEKI_EXPORT
    Deki::ObjectRef<ButtonComponent> button;

    // Sprite to modify
    DEKI_EXPORT
    Deki::ObjectRef<SpriteComponent> sprite;

    // Transition mode
    DEKI_EXPORT
    ButtonStyleMode transition;

    // --- ColorTint mode colors ---
    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    Deki::Color normalColor;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    Deki::Color hoveredColor;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    Deki::Color pressedColor;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    Deki::Color disabledColor;

    // --- SpriteSwap mode sprites ---
    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    Deki::AssetRef<Sprite> normalSprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    Deki::AssetRef<Sprite> hoveredSprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    Deki::AssetRef<Sprite> pressedSprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    Deki::AssetRef<Sprite> disabledSprite;

    ButtonStyleComponent();

    void Start() override;

private:
    void ApplyState(ButtonState state);
};

// Generated property metadata (after class definition for offsetof)
#include "generated/ButtonStyleComponent.gen.h"
