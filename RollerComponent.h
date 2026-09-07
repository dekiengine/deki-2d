#pragma once

#include <stdint.h>
#include <functional>
#include <vector>
#include <string>

#include <deki/Behaviour.h>
#include <deki/Math.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/Color.h>
#include <deki/reflection/Property.h>

// Forward declarations
namespace Deki { class Object; }
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
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Picker wheel: spins through a list of values with momentum and snaps to one.")
class RollerComponent : public Deki::Behaviour
{
public:

    // Expose base class Update() method (RollerComponent::Update(float) has different signature)
    using Deki::Behaviour::Update;

    // ========================================================================
    // Editor-visible properties
    // ========================================================================

    // InputCollider reference (required for receiving input)
    DEKI_EXPORT
    Deki::ObjectRef<InputCollider> inputCollider;

    /** @brief Width of the roller in meters */
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float width;

    /** @brief Height of each item row in meters */
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float itemHeight;

    /** @brief Number of visible rows (should be odd for centered selection) */
    DEKI_EXPORT
    int32_t visibleRows;

    /** @brief Enable infinite scrolling (wrap around) */
    DEKI_EXPORT
    bool infiniteScroll;

    /** @brief Reverse drag direction (default: drag follows touch) */
    DEKI_EXPORT
    bool reverseDrag;

    /** @brief List of options (editable in editor) */
    DEKI_EXPORT
    std::vector<std::string> options;

    /** @brief Currently selected index */
    DEKI_EXPORT
    int32_t selectedIndex;

    // Visual properties are edited directly on child objects:
    // - Background child: SpriteComponent (sprite, tintColor)
    // - Selection child: SpriteComponent (sprite, tintColor)
    // - TextRow children: TextComponent (font, fontSize, color)

    /** @brief Physics deceleration rate (0.0-1.0, higher = slower stop) */
    DEKI_EXPORT
    float deceleration;

    /** @brief Snap animation speed (higher = faster) */
    DEKI_EXPORT
    float snapSpeed;

    /** @brief Height of the selected (center) item row in meters */
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float selectedItemHeight;

    /** @brief Color for selected (center) item text */
    DEKI_EXPORT
    Deki::Color selectedColor;

    /** @brief Color for non-selected item text */
    DEKI_EXPORT
    Deki::Color normalColor;

#ifdef DEKI_EDITOR
    /** @brief Font size for selected item (editor only, for sharp text) */
    DEKI_EXPORT
    DEKI_EDITOR_ONLY
    int32_t selectedFontSize;

    /** @brief Font size for non-selected items (editor only) */
    DEKI_EXPORT
    DEKI_EDITOR_ONLY
    int32_t normalFontSize;
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
    int32_t GetSelectedIndex() const { return selectedIndex; }

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
     * @brief Get the total height of the roller visible area (meters)
     */
    float GetHeight() const
    {
        return ((((static_cast<float>(visibleRows - 1)) * (itemHeight))) + (selectedItemHeight));
    }

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
    void EnsureChildObjects(Deki::Object* owner);
    void SyncChildObjects(Deki::Object* owner);

private:
    // Child object references (found by name, not owned - children of owner Deki::Object)
    Deki::Object* m_ClipObj = nullptr;
    Deki::Object* m_BackgroundObj = nullptr;
    Deki::Object* m_SelectionObj = nullptr;
    std::vector<Deki::Object*> m_TextRowObjs;
    size_t m_DiscoveredOwnerChildren = static_cast<size_t>(-1);
    size_t m_DiscoveredClipChildren = static_cast<size_t>(-1);
    bool NeedsChildDiscovery(const Deki::Object* owner) const;

    // Child object management helpers
    Deki::Object* FindOrCreateChild(Deki::Object* owner, const char* name, const char* componentType);
    void UpdateTextRowCount(Deki::Object* owner);

    /** @brief Selection change callback */
    RollerCallback m_OnSelectionChanged;

    /** @brief Value committed callback (fires when scroll settles) */
    RollerCallback m_OnValueCommitted;

    // Scroll state (float meters; matches the engine's meters-internal convention).
    /** @brief Current scroll offset (m) */
    float m_ScrollOffset;

    /** @brief Current scroll velocity (m/frame) */
    float m_ScrollVelocity;

    /** @brief Target scroll offset for snap animation (m) */
    float m_TargetOffset;

    /** @brief Is currently snapping to an item */
    bool m_IsSnapping;

    /** @brief Programmatic change needs visual sync */
    bool m_NeedsSync = false;

    /** @brief Is currently being dragged */
    bool m_IsDragging;

    /** @brief Last touch Y position (m) */
    float m_LastTouchY;

    /** @brief Touch start Y position (m) */
    float m_TouchStartY;

    /** @brief Touch start scroll offset (m) */
    float m_TouchStartOffset;

    /** @brief Last synced selectedIndex (to detect external changes) */
    int32_t m_LastSyncedSelectedIndex = -1;

    // Sentinel "never synced" stored as a negative float so the != check
    // catches the first sync regardless of mode. Initialized in the ctor.
    float m_LastSyncedSelectedItemHeight;
    float m_LastSyncedItemHeight;

    /**
     * @brief Get scroll offset for a specific item index (m)
     */
    float GetItemOffset(int32_t index) const;

    /**
     * @brief Get the item index at the current scroll position
     */
    int32_t GetIndexAtOffset(float offset) const;

    /**
     * @brief Get the raw (unwrapped) item index at the current scroll position
     * For infinite scroll, does NOT wrap — can return negative or >= options.size()
     */
    int32_t GetRawIndexAtOffset(float offset) const;

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
    void HandlePointerDown(float x, float y);
    void HandlePointerMove(float x, float y);
    void HandlePointerUp(float x, float y);
};

// Generated property metadata (after class definition for offsetof)
