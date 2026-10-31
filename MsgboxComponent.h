#pragma once

#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

#include <deki/Component.h>
#include <deki/reflection/Property.h>
#include "FocusSystem.h"

namespace Deki
{
class Object;
}

namespace Deki2D
{

/// A dialog over the screen: a title, a message and up to three buttons.
/// While it is open, a dimmed backdrop takes the taps meant for what is
/// behind, and the arrow keys stay among its buttons. A button press closes
/// it and says which button it was; Esc closes it as a cancel, when allowed.
///
/// Its parts are children of a Box the editor makes: Backdrop, Panel,
/// Title, Message, and Button0, Button1, Button2, each a ButtonComponent with
/// a Label.
///
/// Example:
///
///     auto* box = entity->GetComponent<MsgboxComponent>();
///     box->AddOnClosedCallback([](int32_t button) { if (button == 0) Quit(); });
///     box->Open();
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A dialog with a title, a message and buttons.")
class MsgboxComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The dialog's title.")
    std::string title = "Title";

    DEKI_EXPORT
    DEKI_TOOLTIP("The message under the title. Long lines wrap.")
    std::string message = "Message";

    DEKI_EXPORT
    DEKI_TOOLTIP("The button labels, left to right, up to three. The first is drawn as the main one.")
    std::vector<std::string> buttons = { "OK", "Cancel" };

    DEKI_EXPORT
    DEKI_TOOLTIP("Whether the dialog shows. Open and Close set it from code; tick it to see the dialog while "
                 "editing.")
    bool isOpen = false;

    DEKI_EXPORT
    DEKI_TOOLTIP("Esc closes the dialog as a cancel, with no button pressed.")
    bool closeOnEsc = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the dialog in meters.")
    DEKI_UNIT(Distance)
    float width = 14.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the dialog in meters.")
    DEKI_UNIT(Distance)
    float height = 8.0f;

    // Runtime-only listeners: the button pressed, counting from 0, or -1
    // for a cancel.
    std::vector<std::function<void(int32_t button)>> onClosed;

    MsgboxComponent() = default;
    ~MsgboxComponent() override;

    void Open();

    /// Closes it, telling the listeners `button` (-1 for a cancel).
    void Close(int32_t button = -1);

    void AddOnClosedCallback(const std::function<void(int32_t)>& callback);

    /// Places and sizes the parts, making any the editor is missing.
    void SyncParts();

    void Start() override;
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

    // IFocusable: the dialog owns its focus scope and takes the keys its
    // buttons leave (Esc); it never takes the focus itself.
    Deki::Object* GetFocusObject() const override { return GetOwner(); }
    bool CanFocus() const override { return false; }
    void OnFocusChanged(bool focused) override { (void)focused; }
    bool OnFocusKey(uint32_t key, uint32_t character) override;

private:
    Deki::Object* m_Box = nullptr;
    Deki::Object* m_Backdrop = nullptr;
    Deki::Object* m_Panel = nullptr;
    Deki::Object* m_Title = nullptr;
    Deki::Object* m_Message = nullptr;
    std::vector<Deki::Object*> m_Buttons;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    size_t m_KnownBoxChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    bool m_Started = false;
    bool m_ScopeOpen = false;

    void EnsureParts();
    void LayoutParts();
    void WireButtons();
    void ShowBox();
};

}  // namespace Deki2D
