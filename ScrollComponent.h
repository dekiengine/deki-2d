#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include "../../DekiBehaviour.h"
#include "../../assets/AssetRef.h"
#include "../../reflection/ObjectRef.h"
#include "reflection/DekiProperty.h"

// Forward declarations
class DekiObject;
class ClipComponent;
class ScrollElement;
class Prefab;
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
using ScrollItemCallback = std::function<void(DekiObject* slot, int32_t itemIndex)>;

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
class ScrollComponent : public DekiBehaviour
{
    DEKI_COMPONENT(ScrollComponent, DekiBehaviour, "2D", "2d1b84cb-8881-4457-8361-98dec7a84498", "DEKI_FEATURE_SCROLL")
    using DekiBehaviour::Update;

public:

    // === Editor-visible properties ===

    // InputCollider reference (required for receiving input)
    DEKI_EXPORT
    ObjectRef<InputCollider> input_collider;

    DEKI_EXPORT
    ScrollMode mode;

    DEKI_VISIBLE_WHEN(mode, Template)
    DEKI_EXPORT
    Deki::AssetRef<Prefab> item_prefab;

    DEKI_EXPORT
    ScrollDirection direction;

    DEKI_EXPORT
    int32_t item_spacing;

    DEKI_EXPORT
    int32_t padding_top;

    DEKI_EXPORT
    int32_t padding_bottom;

    DEKI_EXPORT
    int32_t padding_left;

    DEKI_EXPORT
    int32_t padding_right;

    DEKI_EXPORT
    float deceleration;

    DEKI_EXPORT
    float bounce_stiffness;

    DEKI_EXPORT
    bool enable_bounce;

    DEKI_EXPORT
    bool enable_inertia;

    DEKI_EXPORT
    bool reverse_drag;

    // === Construction ===

    ScrollComponent();
    virtual ~ScrollComponent();

    // === Public API ===

    int32_t GetViewportWidth() const;
    int32_t GetViewportHeight() const;

    void SetItemSpacing(int32_t spacing);
    void SetItemCount(int32_t count);
    void SetDirection(ScrollDirection dir);

    void SetOnBindItem(const ScrollItemCallback& callback);

    void ScrollTo(int32_t position, bool smooth = true);
    void ScrollToItem(int32_t index, bool smooth = true);

    int32_t GetScrollPosition() const;
    int32_t GetFirstVisibleIndex() const { return m_FirstVisibleIndex; }
    int32_t GetLastVisibleIndex() const { return m_LastVisibleIndex; }

    int32_t GetSlotCount() const;
    DekiObject* GetSlotObject(int32_t slotIndex) const;
    int32_t GetSlotItemIndex(int32_t slotIndex) const;

    // === Lifecycle ===

    void Start() override;
    bool NeedsRuntimeUpdate() const override;
    void RuntimeUpdate(float deltaTime) override;
    void OnPropertyChanged(const char* propertyName) override;

    // === Child management (public for editor) ===

    void EnsureChildObjects(DekiObject* owner);
    void SyncChildObjects(DekiObject* owner);

    // === Physics ===

    void Update(float delta_time);

private:
    // Child object references
    DekiObject* m_ClipObj = nullptr;
    DekiObject* m_TemplateObj = nullptr;              // Template mode: blueprint child (hidden)
    std::vector<DekiObject*> m_SlotObjs;              // Template mode: pool slots
    std::vector<int32_t> m_SlotItemIndices;           // Template mode: item index per slot (-1 = unassigned)
    std::vector<DekiObject*> m_NonTemplateChildren;    // NonTemplate mode: all item children
    std::vector<int32_t> m_NonTemplateSizes;             // NonTemplate mode: per-child size in scroll direction
    std::vector<int32_t> m_NonTemplateOffsets;           // NonTemplate mode: prefix-sum offsets, size = itemCount + 1

    // Computed from ScrollElement
    int32_t m_ItemSize = 0;
    int32_t m_ItemCount = 0;

    // Scroll state (Q16.16 fixed-point)
    int32_t m_ScrollOffset = 0;
    int32_t m_ScrollVelocity = 0;

    // Smooth scroll animation
    int32_t m_ScrollTarget = 0;
    bool m_IsSmoothScrolling = false;

    // Touch tracking
    bool m_IsDragging = false;
    bool m_DragConfirmed = false;
    float m_DragThreshold = 5.0f;
    int32_t m_LastTouchPos = 0;
    int32_t m_TouchStartPos = 0;
    int32_t m_TouchStartOffset = 0;

    // Velocity ring buffer for smooth momentum
    static constexpr int kVelocitySamples = 5;
    int32_t m_VelSamples[kVelocitySamples] = {};
    int32_t m_VelSampleIdx = 0;
    int32_t m_VelSampleCount = 0;

    // Computed state
    int32_t m_ContentSize = 0;
    int32_t m_FirstVisibleIndex = 0;
    int32_t m_LastVisibleIndex = 0;

    // Callback (Template mode)
    ScrollItemCallback m_OnBindItem;

    // Input handlers (registered on InputCollider in Start())
    void HandlePointerDown(int32_t x, int32_t y);
    void HandlePointerMove(int32_t x, int32_t y);
    void HandlePointerUp(int32_t x, int32_t y);

    // Helpers
    int32_t MeasureChildSize(DekiObject* child) const;
    int32_t GetTotalSlotCount() const;
    void CalculateContentSize();
    void AssignSlots();
    int32_t GetMaxScrollOffset() const;
    void ClampScrollOffset();

    DekiObject* CloneTemplate(DekiObject* tmpl, const char* name);
    void CancelChildInput(DekiObject* obj);

    static inline int32_t FixedToInt(int32_t fixed) { return fixed >> 16; }
    static inline int32_t IntToFixed(int32_t integer) { return integer << 16; }
};

// Generated property metadata (after class definition for offsetof)
#include "generated/ScrollComponent.gen.h"
