#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include <deki/reflection/ObjectRef.h>

// Forward declarations
namespace Deki { class Object; }
class InputCollider;

/**
 * @brief Button states for interaction feedback
 */
enum class ButtonState : uint8_t
{
    Normal = 0,    // Default state
    Hovered = 1,   // Mouse/finger hovering over button
    Pressed = 2,   // Button is being pressed
    Disabled = 3   // Button is disabled and cannot be interacted with
};

/**
 * @brief Button callback function type
 */
using ButtonCallback = std::function<void()>;

/**
 * @brief Pure interaction component for clickable buttons
 *
 * ButtonComponent handles interaction logic (clicks, hover, state).
 * Requires an InputCollider component on the same or related object
 * to receive input events (like Unity's Collider2D requirement).
 *
 * Features:
 * - Multiple interaction states (normal, hovered, pressed, disabled)
 * - Callback system for click, press, release, hover events
 * - Configurable via ObjectRef to InputCollider
 *
 * Usage example:
 * @code
 * auto* entity = new Deki::Object("Button");
 * auto* collider = entity->AddComponent<InputCollider>();
 * collider->width = 100;
 * collider->height = 40;
 * auto* button = entity->AddComponent<ButtonComponent>();
 * button->inputCollider.Set(entity);
 *
 * button->AddOnClickCallback([]() {
 *     // Handle button click
 * });
 * @endcode
 */
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Makes the object a button: tracks hover and press, and fires a click callback.")
class ButtonComponent : public Deki::Component
{
   public:

    // InputCollider reference (required for receiving input)
    DEKI_EXPORT
    Deki::ObjectRef<InputCollider> inputCollider;

    // State management
    DEKI_EXPORT
    ButtonState state;
    DEKI_EXPORT
    bool isEnabled;

    // Callbacks (not exposed to editor - runtime only)
    // Multiple listeners supported via Add*Callback methods
    std::vector<ButtonCallback> onClick;
    std::vector<ButtonCallback> onPress;
    std::vector<ButtonCallback> onRelease;
    std::vector<ButtonCallback> onHoverEnter;
    std::vector<ButtonCallback> onHoverExit;
    std::vector<std::function<void(ButtonState)>> on_state_changed;

    ButtonComponent();

    virtual ~ButtonComponent();

    // Deki::Component lifecycle
    void Start() override;

    // State management
    /**
     * @brief Set the button state
     * @param new_state New state to set
     */
    void SetState(ButtonState new_state);

    /**
     * @brief Get current button state
     */
    ButtonState GetState() const { return state; }

    /**
     * @brief Enable or disable the button
     * @param enabled true to enable, false to disable
     */
    void SetEnabled(bool enabled);

    /**
     * @brief Check if button is enabled
     */
    bool IsEnabled() const { return isEnabled; }

    // Callback registration (supports multiple listeners)
    void AddOnClickCallback(const ButtonCallback& callback);
    void AddOnPressCallback(const ButtonCallback& callback);
    void AddOnReleaseCallback(const ButtonCallback& callback);
    void AddOnHoverEnterCallback(const ButtonCallback& callback);
    void AddOnHoverExitCallback(const ButtonCallback& callback);
    void AddOnStateChangedCallback(const std::function<void(ButtonState)>& callback);

    /**
     * @brief Cancel any ongoing press (e.g., when scrolling starts)
     * This resets the button state without triggering click callback
     */
    void CancelPress();

   private:
    bool m_WasPressedInside = false;

    void InvokeCallbacks(const std::vector<ButtonCallback>& callbacks);
};

// Generated property metadata (after class definition for offsetof)
