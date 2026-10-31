#pragma once

#include <stdint.h>
#include <functional>
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
class TextareaComponent;

/// Which keys an on-screen keyboard shows.
enum class KeyboardMode : uint8_t
{
    Lower = 0,
    Upper = 1,
    Symbols = 2
};

/// What a key on the on-screen keyboard does.
enum class KeyboardAction : uint8_t
{
    Type = 0,
    Shift = 1,
    Backspace = 2,
    Enter = 3,
    Space = 4,
    Symbols = 5,
    Letters = 6
};

/// A keyboard on the screen, for boards with a touch screen and no keys of
/// their own. It types into the TextareaComponent named by `target`. With
/// keyboard focus (a trackball, say) the arrows move between its keys and
/// Enter presses one.
///
/// All the key caps are one mask (Caps, and Specials for shift, delete and
/// the like), so the keyboard draws as two shapes however many keys it has;
/// each label is a small text, Key0, Key1 and so on; Press marks the key
/// held or focused.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A keyboard on the screen that types into a text box.")
class KeyboardComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that catches presses on the keys. The editor adds one on the same object and sizes "
                 "it to the keyboard.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP("The text box the keys type into.")
    Deki::ObjectRef<TextareaComponent> target;

    DEKI_EXPORT
    DEKI_TOOLTIP("Lower case, upper case, or numbers and symbols.")
    KeyboardMode mode = KeyboardMode::Lower;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled keyboard ignores presses.")
    bool isEnabled = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the keyboard in meters.")
    DEKI_UNIT(Distance)
    float width = 19.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the keyboard in meters, four rows of keys.")
    DEKI_UNIT(Distance)
    float height = 7.0f;

    // Runtime-only listeners: the key's text for a key that types, else "".
    std::vector<std::function<void(KeyboardAction action, const char* text)>> onKey;

    KeyboardComponent() = default;
    ~KeyboardComponent() override;

    /// Presses key `index` of the current layout, as a tap does.
    void PressKey(int32_t index);

    /// How many keys the current layout has.
    int32_t GetKeyCount() const;

    /// The key under a world point, or -1.
    int32_t KeyAt(float worldX, float worldY) const;

    /// The label of key `index` in the current layout.
    const char* GetKeyLabel(int32_t index) const;

    /// The key the arrows have reached, or -1.
    int32_t GetHighlighted() const { return m_Highlight; }

    void AddOnKeyCallback(const std::function<void(KeyboardAction, const char*)>& callback);

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

private:
    Deki::Object* m_Caps = nullptr;
    Deki::Object* m_Specials = nullptr;
    Deki::Object* m_Press = nullptr;
    std::vector<Deki::Object*> m_Labels;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    uint64_t m_CapsKey = 0;  // what the caps were last baked for
    bool m_Focused = false;
    int32_t m_Highlight = -1;  // the key the arrows reached
    int32_t m_Held = -1;       // the key under a pointer that is down

    void EnsureParts();
    void LayoutParts();
    void BakeCaps();
    void PlacePress();
    // Key `index`'s box, from the keyboard's centre, in meters.
    void KeyBox(int32_t index, float& x, float& y, float& w, float& h) const;
    // The nearest key in the row `rowStep` up (negative) or down from `from`.
    int32_t KeyInRow(int32_t from, int32_t rowStep) const;
};

}  // namespace Deki2D
