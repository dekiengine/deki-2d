#pragma once

#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

#include <deki/Component.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/Property.h>
#include "FocusSystem.h"

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

/// A box to type text into: a name, a password, a chat line. A tap gives it
/// the focus; then a keyboard types into it, a real one (the T-Deck's, a
/// desktop's) or a KeyboardComponent on screen. Backspace deletes, and
/// Enter submits a one-line box or starts a new line in a taller one.
///
/// Its parts are children the editor makes: Box, Text, Placeholder, Cursor
/// and Focus.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A box to type text into, from a keyboard or one on screen.")
class TextareaComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that takes the tap that starts typing. The editor adds one on the same object and "
                 "sizes it to the box.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP("The text typed so far.")
    std::string text;

    DEKI_EXPORT
    DEKI_TOOLTIP("Shown dimmed while the box is empty.")
    std::string placeholder = "Type here";

    DEKI_EXPORT
    DEKI_TOOLTIP("The most characters it takes. 0 for no limit.")
    DEKI_RANGE(0, 1024)
    int32_t maxLength = 32;

    DEKI_EXPORT
    DEKI_TOOLTIP("One line: Enter submits. Off, Enter starts a new line and long lines wrap.")
    bool oneLine = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Shows a star for each character instead of the text.")
    bool password = false;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled box ignores taps and typing.")
    bool isEnabled = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the box in meters.")
    DEKI_UNIT(Distance)
    float width = 10.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the box in meters.")
    DEKI_UNIT(Distance)
    float height = 1.75f;

    // Runtime-only listeners
    std::vector<std::function<void(const std::string&)>> onTextChanged;
    std::vector<std::function<void(const std::string&)>> onSubmit;

    TextareaComponent() = default;
    ~TextareaComponent() override;

    /// Replaces the text, cut to Max Length.
    void SetText(const std::string& newText);

    /// Types `value` at the end, as far as Max Length allows. Returns false
    /// when nothing fitted.
    bool InsertText(const char* value);

    /// Deletes the last character, a whole UTF-8 sequence.
    void Backspace();

    /// Enter: submits a one-line box, starts a new line in a taller one.
    void Enter();

    void AddOnTextChangedCallback(const std::function<void(const std::string&)>& callback);
    void AddOnSubmitCallback(const std::function<void(const std::string&)>& callback);

    /// Places and sizes the parts, making any the editor is missing.
    void SyncParts();

    void Start() override;
    bool NeedsRuntimeUpdate() const override { return true; }
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

    // IFocusable
    Deki::Object* GetFocusObject() const override { return GetOwner(); }
    bool CanFocus() const override { return isEnabled; }
    void GetFocusSize(float& w, float& h) const override
    {
        w = width;
        h = height;
    }
    void OnFocusChanged(bool focused) override;
    bool OnFocusKey(uint32_t key, uint32_t character) override;

    bool HasFocus() const { return m_Focused; }

private:
    Deki::Object* m_Box = nullptr;
    Deki::Object* m_Text = nullptr;
    Deki::Object* m_Placeholder = nullptr;
    Deki::Object* m_Cursor = nullptr;
    Deki::Object* m_Focus = nullptr;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    bool m_Focused = false;
    bool m_Pressed = false;
    float m_Blink = 0.0f;
    std::string m_Shown;  // what the Text part shows: the text, or stars

    void EnsureParts();
    void LayoutParts();
    void Changed();
    // Where the cursor goes, from the box's centre, in meters.
    void CursorPosition(float& x, float& y) const;
};

}  // namespace Deki2D
