#pragma once

#include <stdint.h>

#include <deki/Component.h>
#include <deki/assets/AssetRef.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/Property.h>
#include <deki/Color.h>
#include "Sprite.h"

namespace Deki2D
{

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
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Gives a button its look per state, by tinting or by swapping sprites.")
DEKI_FORMER_NAME("ButtonStyleComponent")
class ButtonStyleComponent : public Deki::Component
{

public:

    // Button to observe (required)
    DEKI_EXPORT
    DEKI_TOOLTIP("The button whose state drives this styling. Usually the button on the same object.")
    Deki::ObjectRef<ButtonComponent> button;

    // Sprite to modify
    DEKI_EXPORT
    DEKI_TOOLTIP("The sprite this styling changes. Usually the sprite on the same object.")
    Deki::ObjectRef<SpriteComponent> sprite;

    // Transition mode
    DEKI_EXPORT
    DEKI_TOOLTIP("Whether pressing swaps the tint colour or swaps the sprite outright. Colour is cheaper; separate sprites let the shape change.")
    ButtonStyleMode transition;

    // --- ColorTint mode colors ---
    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    DEKI_TOOLTIP("Tint while the button is idle.")
    Deki::Color normalColor;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    DEKI_TOOLTIP("Tint while the pointer is over it. On a touch-only device this is rarely seen.")
    Deki::Color hoveredColor;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    DEKI_TOOLTIP("Tint while it is held down. This is the feedback that tells someone the press registered.")
    Deki::Color pressedColor;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    DEKI_TOOLTIP("Tint while the button is disabled. Usually faded or desaturated.")
    Deki::Color disabledColor;

    // --- SpriteSwap mode sprites ---
    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    DEKI_TOOLTIP("Sprite while idle, in sprite-swap mode.")
    Deki::AssetRef<Sprite> normalSprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    DEKI_TOOLTIP("Sprite while hovered, in sprite-swap mode.")
    Deki::AssetRef<Sprite> hoveredSprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    DEKI_TOOLTIP("Sprite while held, in sprite-swap mode.")
    Deki::AssetRef<Sprite> pressedSprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    DEKI_TOOLTIP("Sprite while disabled, in sprite-swap mode.")
    Deki::AssetRef<Sprite> disabledSprite;

    ButtonStyleComponent();

    void Start() override;

private:
    void ApplyState(ButtonState state);
};

// Generated property metadata (after class definition for offsetof)

}  // namespace Deki2D
