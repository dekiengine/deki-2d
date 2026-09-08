#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include <deki/Component.h>
#include <deki/Math.h>
#include <deki/assets/AssetRef.h>
#include <deki/reflection/ObjectRef.h>
#include <deki/reflection/Property.h>

// Forward declarations
namespace Deki { class Object; }
class ClipComponent;
class ScrollElement;
namespace Deki { class Scene; }
class InputCollider;

/**
 * @brief Scroll direction enum
 */
enum class ScrollDirection : uint8_t
{
    Vertical = 0,
    Horizontal = 1
};

/**
 * @brief Scroll mode enum
 */
enum class ScrollMode : uint8_t
{
    NonTemplate = 0,  // User manages all children directly
    Template = 1      // Template child cloned into pooled slots with bind callback
};

/**
 * @brief Callback for binding item data to a slot object in Template mode
 */
using ScrollItemCallback = std::function<void(Deki::Object* slot, int32_t itemIndex)>;

/**
 * @brief Optimized scroll component with child object pooling/reuse
 *
 * Supports two modes:
 * - NonTemplate: User pre-creates all item children. ScrollComponent manages
 *   their visibility and positioning based on scroll offset.
 * - Template: User designs a "Template" child. ScrollComponent clones it
 *   to create a pool of reusable slots. A bind callback fires only when
 *   a slot's assigned item index changes (reuse optimization).
 *
 * Features:
 * - Virtual scrolling (only visible items are active)
 * - Momentum-based scrolling with inertia
 * - Edge bounce physics
 * - Fixed-point Q16.16 math for sub-pixel precision
 * - Touch/drag input support
 * - Vertical and horizontal scrolling
 */
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Scrolls its children by dragging, with momentum.")
class ScrollComponent : public Deki::Component
{
    public:
    using Deki::Component::Update;

public:

    // === Editor-visible properties ===

    // InputCollider reference (required for receiving input)
    DEKI_EXPORT
    Deki::ObjectRef<InputCollider> inputCollider;

    DEKI_EXPORT
    ScrollMode mode = ScrollMode::NonTemplate;

    DEKI_VISIBLE_WHEN(mode, Template)
    DEKI_EXPORT
    Deki::AssetRef<Deki::Scene> itemScene;

    DEKI_EXPORT
    ScrollDirection direction = ScrollDirection::Vertical;

    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float itemSpacing;

    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float paddingTop;

    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float paddingBottom;

    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float paddingLeft;

    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float paddingRight;

    DEKI_EXPORT
    float deceleration;

    DEKI_EXPORT
    float bounceStiffness;

    DEKI_EXPORT
    bool enableBounce;

    DEKI_EXPORT
    bool enableInertia;

    DEKI_EXPORT
    bool reverseDrag;

    // === Construction ===

    ScrollComponent();
    virtual ~ScrollComponent();

    // === Public API (world-space values use float) ===

    float GetViewportWidth() const;
    float GetViewportHeight() const;

    void SetItemSpacing(float spacing);
    void SetItemCount(int32_t count);
    void SetDirection(ScrollDirection dir);

    void SetOnBindItem(const ScrollItemCallback& callback);

    void ScrollTo(float position, bool smooth = true);
    void ScrollToItem(int32_t index, bool smooth = true);

    float GetScrollPosition() const;
    int32_t GetFirstVisibleIndex() const { return m_FirstVisibleIndex; }
    int32_t GetLastVisibleIndex() const { return m_LastVisibleIndex; }

    int32_t GetSlotCount() const;
    Deki::Object* GetSlotObject(int32_t slotIndex) const;
    int32_t GetSlotItemIndex(int32_t slotIndex) const;

    // === Lifecycle ===

    void Start() override;
    bool NeedsRuntimeUpdate() const override;
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

    // === Child management (public for editor) ===

    void EnsureChildObjects(Deki::Object* owner);
    void SyncChildObjects(Deki::Object* owner);

    // === Physics ===

    void Update(float delta_time);

private:
    // All sizes/offsets/positions below are world meters.
    // Child object references
    Deki::Object* m_ClipObj = nullptr;
    Deki::Object* m_TemplateObj = nullptr;              // Template mode: blueprint child (hidden)
    // Child counts at the last discovery. Discovery (name scans, component
    // lookups, prefix sums) ran on every frame of a scroll; it now runs only
    // when the owner's or the clip's child list changed size.
    size_t m_DiscoveredOwnerChildren = static_cast<size_t>(-1);
    size_t m_DiscoveredClipChildren = static_cast<size_t>(-1);
    bool NeedsChildDiscovery(const Deki::Object* owner) const;
    std::vector<Deki::Object*> m_SlotObjs;              // Template mode: pool slots
    std::vector<int32_t> m_SlotItemIndices;           // Template mode: item index per slot (-1 = unassigned)
    std::vector<Deki::Object*> m_NonTemplateChildren;    // NonTemplate mode: all item children
    std::vector<float> m_NonTemplateSizes;          // NonTemplate mode: per-child size in scroll direction (m)
    std::vector<float> m_NonTemplateOffsets;        // NonTemplate mode: prefix-sum offsets, size = itemCount + 1 (m)

    // Computed from ScrollElement
    float m_ItemSize = 0.0f;
    int32_t m_ItemCount = 0;

    // Scroll state (meters)
    float m_ScrollOffset = 0.0f;
    float m_ScrollVelocity = 0.0f;

    // Smooth scroll animation
    float m_ScrollTarget = 0.0f;
    bool m_IsSmoothScrolling = false;

    // Touch tracking
    bool m_IsDragging = false;
    bool m_DragConfirmed = false;
    // Drag threshold: ~10 px at ppm=16 (preserves prior tap/drag feel).
    float m_DragThreshold;
    float m_LastTouchPos    = 0.0f;
    float m_TouchStartPos   = 0.0f;
    float m_TouchStartOffset = 0.0f;

    // Velocity ring buffer for smooth momentum
    static constexpr int kVelocitySamples = 5;
    float m_VelSamples[kVelocitySamples] = {};
    int32_t m_VelSampleIdx = 0;
    int32_t m_VelSampleCount = 0;

    // Computed state
    float m_ContentSize = 0.0f;
    int32_t m_FirstVisibleIndex = 0;
    int32_t m_LastVisibleIndex = 0;

    // Callback (Template mode)
    ScrollItemCallback m_OnBindItem;

    // Input handlers (registered on InputCollider in Start()).
    // Pointer x/y are world meters (input FFI contract).
    void HandlePointerDown(float x, float y);
    void HandlePointerMove(float x, float y);
    void HandlePointerUp(float x, float y);

    // Helpers
    float MeasureChildSize(Deki::Object* child) const;
    int32_t GetTotalSlotCount() const;
    void CalculateContentSize();
    void AssignSlots();
    float GetMaxScrollOffset() const;
    void ClampScrollOffset();

    Deki::Object* CloneTemplate(Deki::Object* tmpl, const char* name);
    void CancelChildInput(Deki::Object* obj);
};

// Generated property metadata (after class definition for offsetof)
