#pragma once

#include <stdint.h>
#include <functional>
#include <vector>
#include <string>

#include <deki/Component.h>
#include <deki/Math.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/Color.h>
#include <deki/reflection/Property.h>

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
class BitmapFont;
class TextComponent;
class SpriteComponent;
class ClipComponent;
class Sprite;

/// Called with the index and text of the selected option.
using RollerCallback = std::function<void(int32_t index, const std::string& value)>;

/// Picker wheel: a list of text options the user drags through, like a phone
/// date picker. It scrolls with momentum, snaps to an option, can wrap around
/// at the ends, and reports selection changes through callbacks.
///
/// Example:
///
///     auto* roller = entity->AddComponent<RollerComponent>();
///     roller->SetOptions({"Option 1", "Option 2", "Option 3"});
///     roller->SetVisibleRowCount(3);
///     roller->SetOnSelectionChanged([](int32_t index, const std::string& value) {
///         // Handle selection
///     });
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Picker wheel: spins through a list of values with momentum and snaps to one.")
class RollerComponent : public Deki::Component
{
public:
    // Update(float) below has a different signature, so it would hide the base Update().
    using Deki::Component::Update;

    // ========================================================================
    // Editor-visible properties
    // ========================================================================

    // The roller receives no input without it.
    DEKI_EXPORT
    DEKI_TOOLTIP("The hit area that catches the drag. Without one the roller cannot be spun.")
    Deki::ObjectRef<DekiInput::InputCollider> inputCollider;

    /// Width of the roller, in meters.
    DEKI_EXPORT
    DEKI_TOOLTIP("Width of the roller in meters.")
    DEKI_UNIT(Distance)
    float width;

    /// Height of an unselected row, in meters.
    DEKI_EXPORT
    DEKI_TOOLTIP("Height of one unselected row, in meters.")
    DEKI_UNIT(Distance)
    float itemHeight;

    /// Rows shown at once. Odd numbers centre the selection.
    DEKI_EXPORT
    DEKI_TOOLTIP("How many rows are shown at once, including the selected one. Odd numbers centre the selection.")
    int32_t visibleRows;

    /// Wraps from the last option to the first.
    DEKI_EXPORT
    DEKI_TOOLTIP("Wrap around from the last option to the first, so the roller spins without ends.")
    bool infiniteScroll;

    /// Inverts the drag direction. Off, the content follows the finger.
    DEKI_EXPORT
    DEKI_TOOLTIP("Invert the drag direction.")
    bool reverseDrag;

    DEKI_EXPORT
    DEKI_TOOLTIP("The list of choices, in order.")
    std::vector<std::string> options;

    DEKI_EXPORT
    DEKI_TOOLTIP("Which option is currently chosen, counting from 0.")
    int32_t selectedIndex;

    // Visuals are edited on the child objects:
    // - Background child: SpriteComponent (sprite, tintColor)
    // - Selection child: SpriteComponent (sprite, tintColor)
    // - TextRow children: TextComponent (font, fontSize, color)

    /// How quickly a flick slows, 0 to 1. Higher stops sooner.
    DEKI_EXPORT
    DEKI_TOOLTIP("How quickly a flick slows down. Higher settles sooner.")
    float deceleration;

    /// Speed of the snap onto the nearest option. Higher is faster.
    DEKI_EXPORT
    DEKI_TOOLTIP("How fast the roller settles onto the nearest option once it has slowed. Higher snaps harder.")
    float snapSpeed;

    /// Height of the selected (centre) row, in meters.
    DEKI_EXPORT
    DEKI_TOOLTIP("Height of the selected row, in meters. Making it taller than the others is what marks the selection.")
    DEKI_UNIT(Distance)
    float selectedItemHeight;

    DEKI_EXPORT
    DEKI_TOOLTIP("Text colour of the selected row.")
    Deki::Color selectedColor;

    DEKI_EXPORT
    DEKI_TOOLTIP("Text colour of the rows either side.")
    Deki::Color normalColor;

#ifdef DEKI_EDITOR
    /// Font size of the selected row in the editor, so the preview stays sharp.
    DEKI_EXPORT
    DEKI_TOOLTIP("Editor preview size for the selected row. The device draws at the font asset's baked size.")
    DEKI_EDITOR_ONLY
    int32_t selectedFontSize;

    /// Font size of the unselected rows in the editor.
    DEKI_EXPORT
    DEKI_TOOLTIP("Editor preview size for the unselected rows.")
    DEKI_EDITOR_ONLY
    int32_t normalFontSize;
#endif

    // ========================================================================
    // Runtime state (not serialized)
    // ========================================================================

    RollerComponent();

    virtual ~RollerComponent();

    void SetOptions(const std::vector<std::string>& newOptions);

    const std::vector<std::string>& GetOptions() const { return options; }

    /// 0-based.
    int32_t GetSelectedIndex() const { return selectedIndex; }

    /// The selected option's text, or an empty string when nothing is selected.
    std::string GetSelectedValue() const;

    /// Selects option `index` (0-based). With `animated`, the roller scrolls
    /// there smoothly instead of jumping.
    void SetSelectedIndex(int32_t index, bool animated = false);

    /// Sets how many rows show. Odd numbers centre the selection.
    void SetVisibleRowCount(int32_t rows);

    /// Called whenever the selection changes, including during a drag.
    void SetOnSelectionChanged(const RollerCallback& callback);

    /// Called once the roller has stopped and settled on a value, after the
    /// snap animation ends.
    void SetOnValueCommitted(const RollerCallback& callback);

    /// Advances scrolling and the snap animation by `deltaTime` seconds.
    void Update(float deltaTime);

    /// Total height of the visible area, in meters.
    float GetHeight() const
    {
        return ((((static_cast<float>(visibleRows - 1)) * (itemHeight))) + (selectedItemHeight));
    }

    // ========== Lifecycle methods for Play Mode ==========
    void Start() override;
    bool NeedsRuntimeUpdate() const override;
    void RuntimeUpdate(float deltaTime) override;

    // ========== Editor property change handling ==========
    /// Updates the child TextComponents when a roller property changes.
    void OnPropertyChanged(const char* propertyName) override;

public:
    // Child objects, for editor rendering.
    SpriteComponent* GetBackgroundSprite();
    SpriteComponent* GetSelectionSprite();
    TextComponent* GetTextComponent(int32_t row);
    int32_t GetTextComponentCount() const { return static_cast<int32_t>(m_TextRowObjs.size()); }

    // Public so the editor can call them when it creates the template.
    void EnsureChildObjects(Deki::Object* owner);
    void SyncChildObjects(Deki::Object* owner);

private:
    // Children of the owner, found by name. Not owned.
    Deki::Object* m_ClipObj = nullptr;
    Deki::Object* m_BackgroundObj = nullptr;
    Deki::Object* m_SelectionObj = nullptr;
    std::vector<Deki::Object*> m_TextRowObjs;
    size_t m_DiscoveredOwnerChildren = static_cast<size_t>(-1);
    size_t m_DiscoveredClipChildren = static_cast<size_t>(-1);
    bool NeedsChildDiscovery(const Deki::Object* owner) const;

    Deki::Object* FindOrCreateChild(Deki::Object* owner, const char* name, const char* componentType);
    void UpdateTextRowCount(Deki::Object* owner);

    RollerCallback m_OnSelectionChanged;

    /// Fires when scrolling settles.
    RollerCallback m_OnValueCommitted;

    // Scroll state, in meters like the rest of the engine.
    float m_ScrollOffset;

    /// Meters per frame.
    float m_ScrollVelocity;

    /// Where the snap animation is heading (m).
    float m_TargetOffset;

    bool m_IsSnapping;

    /// Set by a change from code; the visuals still need to catch up.
    bool m_NeedsSync = false;

    bool m_IsDragging;

    /// Last touch Y (m).
    float m_LastTouchY;

    /// Touch Y when the drag began (m).
    float m_TouchStartY;

    /// Scroll offset when the drag began (m).
    float m_TouchStartOffset;

    /// selectedIndex at the last sync, to spot changes made from outside.
    int32_t m_LastSyncedSelectedIndex = -1;

    // Start negative (set in the constructor) to mean "never synced", so the
    // != check catches the first sync in either mode.
    float m_LastSyncedSelectedItemHeight;
    float m_LastSyncedItemHeight;

    /// Scroll offset of item `index` (m).
    float GetItemOffset(int32_t index) const;

    /// The item index at `offset`.
    int32_t GetIndexAtOffset(float offset) const;

    /// The item index at `offset` without wrapping. With infinite scroll it
    /// can be negative or >= options.size().
    int32_t GetRawIndexAtOffset(float offset) const;

    void ClampScrollOffset();

    /// Starts the snap animation to the nearest item.
    void SnapToNearestItem();

    /// Sets the selection from the current scroll position.
    void UpdateSelection();

    // Registered on the InputCollider in Start().
    void HandlePointerDown(float x, float y);
    void HandlePointerMove(float x, float y);
    void HandlePointerUp(float x, float y);
};

}  // namespace Deki2D
