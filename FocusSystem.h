#pragma once

#include <cstddef>
#include <cstdint>

namespace Deki
{
class Object;
}

namespace Deki2D
{

/// A widget that the arrow keys, Enter and a trackball can reach. Widgets
/// register with FocusSystem in Start() and unregister when they go.
class IFocusable
{
public:
    virtual ~IFocusable() = default;

    /// The object the widget sits on. Its world position places it for the
    /// arrow keys, and while it is inactive the widget is skipped.
    virtual Deki::Object* GetFocusObject() const = 0;

    /// True when the widget can take focus now. A disabled widget says no.
    virtual bool CanFocus() const = 0;

    /// The widget's size in meters, centred on its object, so the arrow
    /// keys measure from edge to edge: a short widget beside a long one is
    /// as near as it looks. 0 by 0 measures from the centre.
    virtual void GetFocusSize(float& width, float& height) const
    {
        width = 0.0f;
        height = 0.0f;
    }

    /// Focus arrived (`focused` true) or left. The widget shows or hides
    /// its focus look.
    virtual void OnFocusChanged(bool focused) = 0;

    /// A key pressed while the widget has focus, or while it owns the
    /// focus scope (FocusSystem::PushScope) and the focused widget left the
    /// key unused. `key` is a DekiInput::Keys id, `character` what it types
    /// (0 for none). Returns true when the widget used it; an unused arrow
    /// moves the focus.
    virtual bool OnFocusKey(uint32_t key, uint32_t character) = 0;
};

/// Moves keyboard focus between widgets, as LVGL's groups do.
///
/// The arrow keys go to the focused widget first: a slider takes Left and
/// Right, a roller Up and Down. An arrow it leaves moves the focus to the
/// nearest widget that way, wrapping round at the far side. Tab moves to the
/// next widget in reading order. Nothing has focus until a key is pressed,
/// so a touch screen never shows a focus ring; a press of the pointer or a
/// finger takes the focus away again.
///
/// A trackball in Keys mode is the arrow keys and Enter, so it works the
/// same way.
class FocusSystem
{
public:
    static void Register(IFocusable* widget);

    /// Drops `widget`, taking the focus from it first. Safe for one that
    /// never registered.
    static void Unregister(IFocusable* widget);

    static IFocusable* GetFocused();

    /// Moves the focus to `widget`, or clears it with nullptr.
    static void SetFocus(IFocusable* widget);

    /// Handles one key press: the focused widget first, then the scope's
    /// owner, then focus moves for arrows and Tab. An arrow or Tab with
    /// nothing focused focuses the first widget. Returns true when the key
    /// was used. The input listener calls it; tests call it directly.
    static bool HandleKey(uint32_t key, uint32_t character);

    /// A pointer or finger went down: the focus goes, so no ring shows.
    static void HandlePointerDown();

    /// Limits the focus to `root` and what is under it, as a dialog or an
    /// open list does, until PopScope(root). `owner` gets the keys the
    /// focused widget leaves (Esc to close, say); it may be nullptr.
    static void PushScope(Deki::Object* root, IFocusable* owner);

    /// Ends the scope `root` began, and any begun after it.
    static void PopScope(Deki::Object* root);

    /// Whether moving past the last widget comes round to the first. On by
    /// default.
    static void SetWrap(bool wrap);

    /// How many widgets are registered. For tests.
    static size_t GetWidgetCount();

    // The state lives in the .cpp, so a game's own module reaches it through
    // these functions rather than through data exported from this one.
};

}  // namespace Deki2D
