#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include <deki/reflection/ObjectRef.h>

namespace Deki
{
class Object;
}

namespace DekiInput
{
class InputCollider;
}

namespace Deki2D
{

/// What a button is doing, for visual feedback.
enum class ButtonState : uint8_t
{
    Normal = 0,
    Hovered = 1,  // Mouse or finger over the button
    Pressed = 2,
    Disabled = 3  // Ignores input
};

using ButtonCallback = std::function<void()>;

/// Button behaviour only: clicks, hover and state. Drawing is left to a style
/// component that watches the state.
///
/// It gets input from a DekiInput::InputCollider, on the same object or
/// another one, named by `inputCollider` (as a Unity button needs a
/// Collider2D). It fires callbacks on click, press, release and hover.
///
/// Example:
///
///     auto* entity = new Deki::Object("Button");
///     auto* collider = entity->AddComponent<DekiInput::InputCollider>();
///     collider->width = 100;
///     collider->height = 40;
///     auto* button = entity->AddComponent<ButtonComponent>();
///     button->inputCollider.Set(entity);
///     button->AddOnClickCallback([]() { /* handle the click */ });
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Makes the object a button: tracks hover and press, and fires a click callback.")
DEKI_FORMER_NAME("ButtonComponent")
class ButtonComponent : public Deki::Component
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that makes this button clickable. Without one the button has no way to notice a press.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP(
        "Normal, hovered, pressed or disabled. Set by input; a style component watches it to decide what to draw.")
    ButtonState state;
    DEKI_EXPORT
    DEKI_TOOLTIP(
        "A disabled button ignores input and reports the disabled state, so it can be greyed out rather than hidden.")
    bool isEnabled;

    // Runtime-only listeners, added with the Add*Callback methods
    std::vector<ButtonCallback> onClick;
    std::vector<ButtonCallback> onPress;
    std::vector<ButtonCallback> onRelease;
    std::vector<ButtonCallback> onHoverEnter;
    std::vector<ButtonCallback> onHoverExit;
    std::vector<std::function<void(ButtonState)>> onStateChanged;

    ButtonComponent();

    virtual ~ButtonComponent();

    // Deki::Component lifecycle
    void Start() override;

    /// Sets the state and fires the hover and state-changed callbacks it causes.
    void SetState(ButtonState newState);

    ButtonState GetState() const { return state; }

    /// Enables or disables the button. Disabling it also drops any press in
    /// progress.
    void SetEnabled(bool enabled);

    bool IsEnabled() const { return isEnabled; }

    // Each adds one more listener
    void AddOnClickCallback(const ButtonCallback& callback);
    void AddOnPressCallback(const ButtonCallback& callback);
    void AddOnReleaseCallback(const ButtonCallback& callback);
    void AddOnHoverEnterCallback(const ButtonCallback& callback);
    void AddOnHoverExitCallback(const ButtonCallback& callback);
    void AddOnStateChangedCallback(const std::function<void(ButtonState)>& callback);

    /// Cancels a press in progress, such as when scrolling starts. The button
    /// returns to Normal and fires release but not click.
    void CancelPress();

private:
    bool m_WasPressedInside = false;

    void InvokeCallbacks(const std::vector<ButtonCallback>& callbacks);
};

}  // namespace Deki2D
