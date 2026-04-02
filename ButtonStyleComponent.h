#pragma once

#include <stdint.h>

#include "../../DekiBehaviour.h"
#include "../../assets/AssetRef.h"
#include "../../reflection/ObjectRef.h"
#include "../../reflection/DekiProperty.h"
#include "Color.h"
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
 * auto* entity = new DekiObject("StyledButton");
 * auto* sprite = entity->AddComponent<SpriteComponent>();
 * auto* button = entity->AddComponent<ButtonComponent>();
 * auto* style = entity->AddComponent<ButtonStyleComponent>();
 *
 * // Set button reference (required)
 * style->button.Set(entity);
 *
 * // Configure colors (ColorTint mode)
 * style->normal_color = deki::Color::White;
 * style->pressed_color = deki::Color(160, 160, 160);
 * @endcode
 */
class ButtonStyleComponent : public DekiBehaviour
{
    DEKI_COMPONENT(ButtonStyleComponent, DekiBehaviour, "2D", "0c34e973-6cd1-45d2-b2c6-f010ba320707", "DEKI_FEATURE_BUTTON")

public:

    // Button to observe (required)
    DEKI_EXPORT
    ObjectRef<ButtonComponent> button;

    // Sprite to modify
    DEKI_EXPORT
    ObjectRef<SpriteComponent> sprite;

    // Transition mode
    DEKI_EXPORT
    ButtonStyleMode transition;

    // --- ColorTint mode colors ---
    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    deki::Color normal_color;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    deki::Color hovered_color;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    deki::Color pressed_color;

    DEKI_VISIBLE_WHEN(transition, ColorTint)
    DEKI_EXPORT
    deki::Color disabled_color;

    // --- SpriteSwap mode sprites ---
    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    Deki::AssetRef<Sprite> normal_sprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    Deki::AssetRef<Sprite> hovered_sprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    Deki::AssetRef<Sprite> pressed_sprite;

    DEKI_VISIBLE_WHEN(transition, SpriteSwap)
    DEKI_EXPORT
    Deki::AssetRef<Sprite> disabled_sprite;

    ButtonStyleComponent();

    void Start() override;

private:
    void ApplyState(ButtonState state);
};

// Generated property metadata (after class definition for offsetof)
#include "generated/ButtonStyleComponent.gen.h"
