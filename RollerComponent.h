#pragma once

#include <stdint.h>
#include <functional>
#include <vector>
#include <string>

#include "DekiBehaviour.h"
#include "reflection/ObjectRef.h"
#include "Color.h"
#include "reflection/DekiProperty.h"

// Forward declarations
class DekiObject;
class BitmapFont;
class TextComponent;
class SpriteComponent;
class ClipComponent;
class Sprite;
class InputCollider;

/**
 * @brief Callback function type for roller selection changes
 */
using RollerCallback = std::function<void(int32_t index, const std::string& value)>;

/**
 * @brief Customizable roller/picker wheel component
 *
 * RollerComponent provides a scrollable list of text options that can be
 * selected by touch/drag input, similar to mobile picker widgets.
 *
 * Features:
 * - Configurable number of visible rows
 * - Momentum-based scrolling with snap-to-item
 * - Touch/drag input support
 * - Customizable colors and styling
 * - Selection change callback
 * - Infinite scroll option (wraps around)
 *
 * Usage example:
 * @code
 * auto* roller = entity->AddComponent<RollerComponent>();
 * roller->SetOptions({"Option 1", "Option 2", "Option 3"});
 * roller->SetVisibleRowCount(3);
 * roller->SetOnSelectionChanged([](int32_t index, const std::string& value) {
 *     // Handle selection
 * });
 * @endcode
 */
class RollerComponent : public DekiBehaviour
{
public:
    DEKI_COMPONENT(RollerComponent, DekiBehaviour, "2D", "72e6d6c3-ec5d-4c8f-8411-4d31824b944e", "DEKI_FEATURE_ROLLER")

    // Expose base class Update() method (RollerComponent::Update(float) has different signature)
    using DekiBehaviour::Update;

    // ========================================================================
    // Editor-visible properties
    // ========================================================================

    // InputCollider reference (required for receiving input)
    DEKI_EXPORT
    ObjectRef<InputCollider> input_collider;

    /** @brief Width of the roller in pixels */
    DEKI_EXPORT
    int32_t width;

    /** @brief Height of each item row in pixels */
    DEKI_EXPORT
    int32_t item_height;

    /** @brief Number of visible rows (should be odd for centered selection) */
    DEKI_EXPORT
    int32_t visible_rows;

    /** @brief Enable infinite scrolling (wrap around) */
    DEKI_EXPORT
    bool infinite_scroll;

    /** @brief Reverse drag direction (default: drag follows touch) */
    DEKI_EXPORT
    bool reverse_drag;

    /** @brief List of options (editable in editor) */
    DEKI_EXPORT
    std::vector<std::string> options;

    /** @brief Currently selected index */
    DEKI_EXPORT
    int32_t selected_index;

    // Visual properties are edited directly on child objects:
    // - Background child: SpriteComponent (sprite, tint_color)
    // - Selection child: SpriteComponent (sprite, tint_color)
    // - TextRow children: TextComponent (font, fontSize, color)

    /** @brief Physics deceleration rate (0.0-1.0, higher = slower stop) */
    DEKI_EXPORT
    float deceleration;

    /** @brief Snap animation speed (pixels per frame) */
    DEKI_EXPORT
    float snap_speed;

    /** @brief Height of the selected (center) item row in pixels */
    DEKI_EXPORT
    int32_t selected_item_height;

    /** @brief Color for selected (center) item text */
    DEKI_EXPORT
    deki::Color selected_color;

    /** @brief Color for non-selected item text */
    DEKI_EXPORT
    deki::Color normal_color;

#ifdef DEKI_EDITOR
    /** @brief Font size for selected item (editor only, for sharp text) */
    DEKI_EXPORT
    int32_t selected_font_size;

    /** @brief Font size for non-selected items (editor only) */
    DEKI_EXPORT
    int32_t normal_font_size;
#endif

    // ========================================================================
    // Runtime state (not serialized)
    // ========================================================================

    /**
     * @brief Construct a roller component
     */
    RollerComponent();

    virtual ~RollerComponent();

    /**
     * @brief Set the list of options
     * @param newOptions Vector of option strings
     */
    void SetOptions(const std::vector<std::string>& newOptions);

    /**
     * @brief Get the list of options
     * @return Vector of option strings
     */
    const std::vector<std::string>& GetOptions() const { return options; }

    /**
     * @brief Get the currently selected index
     * @return Selected index (0-based)
     */
    int32_t GetSelectedIndex() const { return selected_index; }

    /**
     * @brief Get the currently selected value
     * @return Selected option string, or empty if no selection
     */
    std::string GetSelectedValue() const;

    /**
     * @brief Set the selected index
     * @param index Index to select (0-based)
     * @param animated Use smooth animation to scroll to selection
     */
    void SetSelectedIndex(int32_t index, bool animated = false);

    /**
     * @brief Set the number of visible rows
     * @param rows Number of rows (should be odd for centered selection)
     */
    void SetVisibleRowCount(int32_t rows);

    /**
     * @brief Set callback for selection changes
     * @param callback Function to call when selection changes
     */
    void SetOnSelectionChanged(const RollerCallback& callback);

    /**
     * @brief Set callback for when roller finishes scrolling and settles on a value
     * Unlike OnSelectionChanged which fires during drag, this only fires once
     * the snap animation completes.
     */
    void SetOnValueCommitted(const RollerCallback& callback);

    /**
     * @brief Update roller physics and animation
     * @param delta_time Time since last frame in seconds
     */
    void Update(float delta_time);

    /**
     * @brief Get the total height of the roller (visible area)
     * @return Height in pixels
     */
    int32_t GetHeight() const { return (visible_rows - 1) * item_height + selected_item_height; }

    // ========== Lifecycle methods for Play Mode ==========
    void Start() override;
    bool NeedsRuntimeUpdate() const override;
    void RuntimeUpdate(float deltaTime) override;

    // ========== Editor property change handling ==========
    /**
     * @brief Handle property changes from editor
     * Syncs child TextComponents when roller properties change
     */
    void OnPropertyChanged(const char* propertyName) override;

public:
    // Accessors for editor rendering (child objects)
    SpriteComponent* GetBackgroundSprite();
    SpriteComponent* GetSelectionSprite();
    TextComponent* GetTextComponent(int32_t row);
    int32_t GetTextComponentCount() const { return static_cast<int32_t>(m_TextRowObjs.size()); }

    // Child object management (public for editor template creation)
    void EnsureChildObjects(DekiObject* owner);
    void SyncChildObjects(DekiObject* owner);

private:
    // Child object references (found by name, not owned - children of owner DekiObject)
    DekiObject* m_ClipObj = nullptr;
    DekiObject* m_BackgroundObj = nullptr;
    DekiObject* m_SelectionObj = nullptr;
    std::vector<DekiObject*> m_TextRowObjs;

    // Child object management helpers
    DekiObject* FindOrCreateChild(DekiObject* owner, const char* name, const char* componentType);
    void UpdateTextRowCount(DekiObject* owner);

    /** @brief Selection change callback */
    RollerCallback m_OnSelectionChanged;

    /** @brief Value committed callback (fires when scroll settles) */
    RollerCallback m_OnValueCommitted;

    /** @brief Current scroll offset in pixels (fixed-point Q16.16) */
    int32_t m_ScrollOffset;

    /** @brief Current scroll velocity (fixed-point Q16.16) */
    int32_t m_ScrollVelocity;

    /** @brief Target scroll offset for snap animation */
    int32_t m_TargetOffset;

    /** @brief Is currently snapping to an item */
    bool m_IsSnapping;

    /** @brief Programmatic change needs visual sync */
    bool m_NeedsSync = false;

    /** @brief Is currently being dragged */
    bool m_IsDragging;

    /** @brief Last touch Y position */
    int32_t m_LastTouchY;

    /** @brief Touch start Y position */
    int32_t m_TouchStartY;

    /** @brief Touch start scroll offset */
    int32_t m_TouchStartOffset;

    /** @brief Last synced selected_index (to detect external changes) */
    int32_t m_LastSyncedSelectedIndex = -1;

    /** @brief Last synced selected_item_height (to detect height changes) */
    int32_t m_LastSyncedSelectedItemHeight = -1;

    /** @brief Last synced item_height (to detect height changes) */
    int32_t m_LastSyncedItemHeight = -1;

    /**
     * @brief Convert fixed-point to integer
     */
    static inline int32_t FixedToInt(int32_t fixed) { return fixed >> 16; }

    /**
     * @brief Convert integer to fixed-point
     */
    static inline int32_t IntToFixed(int32_t integer) { return integer << 16; }

    /**
     * @brief Get scroll offset for a specific item index
     */
    int32_t GetItemOffset(int32_t index) const;

    /**
     * @brief Get the item index at the current scroll position
     */
    int32_t GetIndexAtOffset(int32_t offset) const;

    /**
     * @brief Get the raw (unwrapped) item index at the current scroll position
     * For infinite scroll, does NOT wrap — can return negative or >= options.size()
     */
    int32_t GetRawIndexAtOffset(int32_t offset) const;

    /**
     * @brief Clamp scroll offset to valid range
     */
    void ClampScrollOffset();

    /**
     * @brief Start snap animation to nearest item
     */
    void SnapToNearestItem();

    /**
     * @brief Update selection based on current scroll position
     */
    void UpdateSelection();

    // Input handlers (registered on InputCollider in Start())
    void HandlePointerDown(int32_t x, int32_t y);
    void HandlePointerMove(int32_t x, int32_t y);
    void HandlePointerUp(int32_t x, int32_t y);
};

// Generated property metadata (after class definition for offsetof)
#include "generated/RollerComponent.gen.h"
