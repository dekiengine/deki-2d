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

/// A field that shows the chosen option and opens a list to pick another:
/// a difficulty, a language, a save slot.
///
/// A tap opens the list; a tap on a row picks it, and a tap anywhere else
/// closes the list and reaches nothing else. With keyboard focus, Enter opens it, Up and Down move
/// through it, Enter picks and Esc closes. The list draws above the
/// widgets around it and outside any clipping.
///
/// Its parts are children the editor makes: Field, Label, Arrow and Focus,
/// and the List with its Back, Highlight, Rows and Row0, Row1, ... texts.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A field that opens a list of options to pick one.")
class DropdownComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area of the closed field. The editor adds one on the same object and sizes it to the field.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    DEKI_EXPORT
    DEKI_TOOLTIP("The choices, in order.")
    std::vector<std::string> options = { "Easy", "Normal", "Hard" };

    DEKI_EXPORT
    DEKI_TOOLTIP("Which option is chosen, counting from 0.")
    int32_t selectedIndex = 0;

    DEKI_EXPORT
    DEKI_TOOLTIP("Rows the open list shows at once. With more options it scrolls.")
    DEKI_RANGE(1, 12)
    int32_t listRows = 4;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled dropdown ignores taps and keys.")
    bool isEnabled = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the field and the list, in meters.")
    DEKI_UNIT(Distance)
    float width = 7.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the field, and of each row of the list, in meters.")
    DEKI_UNIT(Distance)
    float height = 1.75f;

    // Runtime-only listeners
    std::vector<std::function<void(int32_t index, const std::string& value)>> onSelectionChanged;

    DropdownComponent() = default;
    ~DropdownComponent() override;

    /// Chooses option `index`. With `notify`, the selection callbacks run.
    void SetSelectedIndex(int32_t index, bool notify = true);

    /// The chosen option's text, or an empty string.
    const std::string& GetSelectedValue() const;

    void AddOnSelectionChangedCallback(const std::function<void(int32_t, const std::string&)>& callback);

    void Open();
    void Close();
    bool IsOpen() const { return m_Open; }

    /// The option the open list has highlighted.
    int32_t GetHighlighted() const { return m_Highlight; }

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
    Deki::Object* m_Field = nullptr;
    Deki::Object* m_Label = nullptr;
    Deki::Object* m_Arrow = nullptr;
    Deki::Object* m_Focus = nullptr;
    Deki::Object* m_List = nullptr;
    Deki::Object* m_Back = nullptr;
    Deki::Object* m_HighlightPart = nullptr;
    Deki::Object* m_Rows = nullptr;
    std::vector<Deki::Object*> m_RowParts;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    size_t m_KnownListChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    bool m_Focused = false;
    bool m_Open = false;
    bool m_Listening = false;
    bool m_RestoreFocus = false;  // the dropdown had the focus when it opened
    int32_t m_Highlight = 0;
    int32_t m_Scroll = 0;  // the option in the top row
    int32_t m_PressedRow = -1;

    void EnsureParts();
    void LayoutParts();
    void ListenToList();
    // The option under a world point in the open list, or -1.
    int32_t OptionAt(float worldY) const;
    void MoveHighlight(int32_t index);
    float ListHeight() const;
};

}  // namespace Deki2D
