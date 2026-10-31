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

/// A column of rows to tap: a settings menu, a file list, a level select.
///
/// A tap on a row clicks it. An item that starts with "# " is a section
/// title: shown dimmed, and neither tapped nor reached with the arrows. With
/// more items than rows a drag scrolls it, and a thumb on the right shows
/// where it is. With keyboard focus, Up and Down move the highlight and Enter
/// clicks the highlighted row.
///
/// Its parts are children the editor makes: Back, Highlight, Thumb, Focus,
/// Rows and Row0, Row1, ... texts.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("A scrolling column of rows to tap, with section titles.")
class ListComponent : public Deki::Component, public IFocusable
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The rows, top to bottom. One that starts with \"# \" is a section title.")
    std::vector<std::string> items = { "# Audio", "Music", "Sound effects", "# Display", "Brightness", "Language" };

    DEKI_EXPORT
    DEKI_TOOLTIP("Rows shown at once. With more items it scrolls.")
    DEKI_RANGE(1, 16)
    int32_t visibleRows = 5;

    DEKI_EXPORT
    DEKI_TOOLTIP("A disabled list ignores taps and keys.")
    bool isEnabled = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the list, in meters.")
    DEKI_UNIT(Distance)
    float width = 8.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Height of each row, in meters.")
    DEKI_UNIT(Distance)
    float rowHeight = 1.5f;

    // Runtime-only listeners
    std::vector<std::function<void(int32_t index, const std::string& text)>> onItemClicked;

    ListComponent() = default;
    ~ListComponent() override;

    void AddOnItemClickedCallback(const std::function<void(int32_t, const std::string&)>& callback);

    /// Clicks item `index`, as a tap on it would. A title is not clicked.
    void Click(int32_t index);

    /// True for an item that is a section title.
    bool IsTitle(int32_t index) const;

    /// An item's text as shown, without a title's "# ".
    std::string GetItemText(int32_t index) const;

    /// The item the arrows have highlighted, or -1.
    int32_t GetHighlighted() const { return m_Highlight; }

    /// The item in the top row.
    int32_t GetScroll() const { return m_Scroll; }

    /// Scrolls so `index` is in the top row, as far as the items allow.
    void ScrollTo(int32_t index);

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
        h = ListHeight();
    }
    void OnFocusChanged(bool focused) override;
    bool OnFocusKey(uint32_t key, uint32_t character) override;

    /// Height of the whole list, in meters.
    float ListHeight() const;

private:
    Deki::Object* m_Back = nullptr;
    Deki::Object* m_HighlightPart = nullptr;
    Deki::Object* m_Thumb = nullptr;
    Deki::Object* m_Focus = nullptr;
    Deki::Object* m_Rows = nullptr;
    std::vector<Deki::Object*> m_RowParts;
    size_t m_KnownChildren = static_cast<size_t>(-1);
    uint64_t m_LastLayout = 0;
    bool m_Focused = false;
    bool m_Listening = false;
    int32_t m_Highlight = -1;
    int32_t m_Scroll = 0;
    int32_t m_PressedItem = -1;
    float m_PressY = 0.0f;
    int32_t m_PressScroll = 0;
    bool m_Dragged = false;

    void EnsureParts();
    void LayoutParts();
    void Listen();
    int32_t RowsShown() const;
    int32_t MaxScroll() const;
    // The item under a world point, or -1.
    int32_t ItemAt(float worldY) const;
    // The next item from `from` that is not a title, going `step`, or -1.
    int32_t NextClickable(int32_t from, int32_t step) const;
    void MoveHighlight(int32_t index);
};

}  // namespace Deki2D
