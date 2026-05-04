#include "ScrollComponent.h"
#include "deki-input/InputCollider.h"
#include "deki-input/InputDispatch.h"
#include "DekiObject.h"
#include "DekiLogSystem.h"
#include "ClipComponent.h"
#include "ScrollElement.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include "Prefab.h"


ScrollComponent::ScrollComponent()
    : DekiBehaviour(),
      mode(ScrollMode::NonTemplate),
      direction(ScrollDirection::Vertical),
      item_spacing(0),
      padding_top(0),
      padding_bottom(0),
      padding_left(0),
      padding_right(0),
      deceleration(0.95f),
      bounce_stiffness(0.15f),
      enable_bounce(true),
      enable_inertia(true),
      reverse_drag(false)
{
}

ScrollComponent::~ScrollComponent()
{
    // Release gesture if we own it (prevents stuck input after destruction mid-drag)
    if (InputDispatch::IsGestureClaimedBy(this))
        InputDispatch::ReleaseGesture();

    m_ClipObj = nullptr;
    m_TemplateObj = nullptr;
    m_SlotObjs.clear();
    m_SlotItemIndices.clear();
    m_NonTemplateChildren.clear();
    m_NonTemplateSizes.clear();
    m_NonTemplateOffsets.clear();
}

// ============================================================================
// Child Object Management
// ============================================================================

static DekiObject* FindChildByName(DekiObject* parent, const char* name)
{
    if (!parent) return nullptr;
    for (auto* child : parent->GetChildren())
    {
        if (child->GetName() == name)
            return child;
    }
    return nullptr;
}

DekiObject* ScrollComponent::CloneTemplate(DekiObject* tmpl, const char* name)
{
    if (!tmpl || !item_prefab.Get()) return nullptr;

    Prefab* ownerPrefab = tmpl->GetOwnerPrefab();
    if (!ownerPrefab) return nullptr;

    DekiObject* instance = item_prefab.Get()->Instantiate(ownerPrefab);
    if (instance)
    {
        ownerPrefab->RemoveObject(instance);
        instance->SetName(name);
        instance->SetOwnerPrefab(ownerPrefab);
    }
    return instance;
}

int32_t ScrollComponent::MeasureChildSize(DekiObject* child) const
{
    if (!child) return 0;

    auto* element = child->GetComponent<ScrollElement>();
    if (!element) return 0;

    return (direction == ScrollDirection::Vertical) ? element->height : element->width;
}

int32_t ScrollComponent::GetTotalSlotCount() const
{
    if (m_ItemSize <= 0) return 3;

    int32_t vpSize = (direction == ScrollDirection::Vertical) ? GetViewportHeight() : GetViewportWidth();
    int32_t visible = (vpSize / m_ItemSize) + 3;  // +1 top buffer, +1 bottom buffer, +1 rounding
    return std::max(static_cast<int32_t>(3), visible);
}

void ScrollComponent::EnsureChildObjects(DekiObject* owner)
{
    if (!owner)
    {
        m_ClipObj = nullptr;
        m_TemplateObj = nullptr;
        m_SlotObjs.clear();
        m_SlotItemIndices.clear();
        m_NonTemplateChildren.clear();
        m_NonTemplateSizes.clear();
        m_NonTemplateOffsets.clear();
        return;
    }

    // Discover Clip child (must already exist; authored in the editor).
    m_ClipObj = FindChildByName(owner, "Clip");
    if (!m_ClipObj)
    {
        DEKI_LOG_WARNING("ScrollComponent: missing 'Clip' child on '%s'",
                         owner->GetName().c_str());
        m_TemplateObj = nullptr;
        m_SlotObjs.clear();
        m_SlotItemIndices.clear();
        m_NonTemplateChildren.clear();
        m_NonTemplateSizes.clear();
        m_NonTemplateOffsets.clear();
        return;
    }

    if (mode == ScrollMode::Template)
    {
        // Find the Template child (must already exist; authored in the editor).
        m_TemplateObj = FindChildByName(m_ClipObj, "Template");
        if (!m_TemplateObj)
        {
            DEKI_LOG_WARNING("ScrollComponent: missing 'Template' child on '%s'",
                             owner->GetName().c_str());
            m_SlotObjs.clear();
            m_SlotItemIndices.clear();
            return;
        }

        m_TemplateObj->SetActive(false);  // Template is always hidden at runtime

        // Measure item size from template
        m_ItemSize = MeasureChildSize(m_TemplateObj);

        // Collect existing Slot children
        m_SlotObjs.clear();
        for (auto* child : m_ClipObj->GetChildren())
        {
            if (child->GetName().rfind("Slot", 0) == 0)
            {
                child->SetActive(true);
                m_SlotObjs.push_back(child);
            }
        }

        // Sort by name for correct order
        std::sort(m_SlotObjs.begin(), m_SlotObjs.end(),
            [](DekiObject* a, DekiObject* b) {
                return a->GetName() < b->GetName();
            });

        int32_t totalSlots = GetTotalSlotCount();

        // Create missing slots by cloning template
        while (static_cast<int32_t>(m_SlotObjs.size()) < totalSlots)
        {
            std::string name = "Slot" + std::to_string(m_SlotObjs.size());
            DekiObject* slot = CloneTemplate(m_TemplateObj, name.c_str());
            if (slot)
            {
                m_ClipObj->AddChild(slot);
                slot->SetActive(true);
                m_SlotObjs.push_back(slot);
            }
            else
            {
                break;
            }
        }

        // Hide excess slots
        for (size_t i = totalSlots; i < m_SlotObjs.size(); ++i)
        {
            m_SlotObjs[i]->SetActive(false);
        }
        if (m_SlotObjs.size() > static_cast<size_t>(totalSlots))
        {
            m_SlotObjs.resize(totalSlots);
        }

        // Initialize slot item indices
        m_SlotItemIndices.resize(m_SlotObjs.size(), -1);
    }
    else // NonTemplate mode
    {
        m_TemplateObj = nullptr;
        m_SlotObjs.clear();
        m_SlotItemIndices.clear();

        // Collect all children under Clip and measure each one
        m_NonTemplateChildren.clear();
        m_NonTemplateSizes.clear();
        for (auto* child : m_ClipObj->GetChildren())
        {
            m_NonTemplateChildren.push_back(child);
            m_NonTemplateSizes.push_back(MeasureChildSize(child));
        }

        m_ItemCount = static_cast<int32_t>(m_NonTemplateChildren.size());

        // Build prefix-sum offsets (one pass)
        m_NonTemplateOffsets.resize(m_ItemCount + 1);
        m_NonTemplateOffsets[0] = 0;
        for (int32_t i = 0; i < m_ItemCount; i++)
        {
            m_NonTemplateOffsets[i + 1] = m_NonTemplateOffsets[i] + m_NonTemplateSizes[i]
                                         + ((i < m_ItemCount - 1) ? item_spacing : 0);
        }

        // Keep m_ItemSize for Template code paths
        m_ItemSize = m_NonTemplateChildren.empty() ? 0 : m_NonTemplateSizes[0];
    }
}

void ScrollComponent::SyncChildObjects(DekiObject* owner)
{
    EnsureChildObjects(owner);

    CalculateContentSize();
    AssignSlots();
}

// ============================================================================
// Recycling Algorithm
// ============================================================================

void ScrollComponent::AssignSlots()
{
    if (!m_ClipObj) return;

    int32_t scrollPos = FixedToInt(m_ScrollOffset);
    int32_t vpSize = (direction == ScrollDirection::Vertical) ? GetViewportHeight() : GetViewportWidth();
    int32_t stride = m_ItemSize + item_spacing;
    if (stride <= 0) return;

    int32_t halfVp = vpSize / 2;
    int32_t startPad = (direction == ScrollDirection::Vertical) ? padding_top : padding_left;
    int32_t crossPad = (direction == ScrollDirection::Vertical)
        ? (padding_left - padding_right) / 2
        : (padding_top - padding_bottom) / 2;

    m_FirstVisibleIndex = m_ItemCount;
    m_LastVisibleIndex = -1;

    if (mode == ScrollMode::Template)
    {
        if (m_SlotObjs.empty() || m_ItemCount <= 0) return;

        // First item index to show (with 1 buffer before viewport)
        int32_t adjustedScroll = scrollPos - startPad;
        int32_t firstIndex = (adjustedScroll / stride) - 1;
        if (firstIndex < 0) firstIndex = 0;

        int32_t totalSlots = static_cast<int32_t>(m_SlotObjs.size());

        for (int32_t s = 0; s < totalSlots; s++)
        {
            int32_t itemIndex = firstIndex + s;

            if (itemIndex < 0 || itemIndex >= m_ItemCount)
            {
                m_SlotObjs[s]->SetActive(false);
                m_SlotItemIndices[s] = -1;
                continue;
            }

            m_SlotObjs[s]->SetActive(true);

            // Position: item pixel offset relative to viewport center
            int32_t pixelPos = startPad + itemIndex * stride - scrollPos;
            float slotCenter = static_cast<float>(pixelPos) + m_ItemSize * 0.5f - halfVp;

            if (direction == ScrollDirection::Vertical)
                m_SlotObjs[s]->SetLocalPosition(static_cast<float>(crossPad), -slotCenter);
            else
                m_SlotObjs[s]->SetLocalPosition(slotCenter, static_cast<float>(crossPad));

            // Track visible range
            if (pixelPos + m_ItemSize > 0 && pixelPos < vpSize)
            {
                if (itemIndex < m_FirstVisibleIndex) m_FirstVisibleIndex = itemIndex;
                if (itemIndex > m_LastVisibleIndex) m_LastVisibleIndex = itemIndex;
            }

            // REUSE OPTIMIZATION: only fire callback if item index changed
            if (m_SlotItemIndices[s] != itemIndex)
            {
                m_SlotItemIndices[s] = itemIndex;
                if (m_OnBindItem)
                {
                    m_OnBindItem(m_SlotObjs[s], itemIndex);
                }
            }
        }
    }
    else // NonTemplate mode
    {
        for (int32_t i = 0; i < static_cast<int32_t>(m_NonTemplateChildren.size()); i++)
        {
            DekiObject* child = m_NonTemplateChildren[i];
            if (!child) continue;

            int32_t itemSize = m_NonTemplateSizes[i];
            int32_t pixelPos = startPad + m_NonTemplateOffsets[i] - scrollPos;

            // Visibility culling with per-item size buffer
            bool visible = (pixelPos + itemSize > -itemSize && pixelPos < vpSize + itemSize);
            child->SetActive(visible);

            if (visible)
            {
                float center = static_cast<float>(pixelPos) + itemSize * 0.5f - halfVp;

                if (direction == ScrollDirection::Vertical)
                    child->SetLocalPosition(static_cast<float>(crossPad), -center);
                else
                    child->SetLocalPosition(center, static_cast<float>(crossPad));
            }

            // Track visible range (within actual viewport, not buffer)
            if (pixelPos + itemSize > 0 && pixelPos < vpSize)
            {
                if (i < m_FirstVisibleIndex) m_FirstVisibleIndex = i;
                if (i > m_LastVisibleIndex) m_LastVisibleIndex = i;
            }
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

int32_t ScrollComponent::GetViewportWidth() const
{
    if (m_ClipObj)
    {
        if (auto* clip = m_ClipObj->GetComponent<ClipComponent>())
            return clip->width;
    }
    return 0;
}

int32_t ScrollComponent::GetViewportHeight() const
{
    if (m_ClipObj)
    {
        if (auto* clip = m_ClipObj->GetComponent<ClipComponent>())
            return clip->height;
    }
    return 0;
}

void ScrollComponent::SetItemSpacing(int32_t spacing)
{
    item_spacing = spacing;
    CalculateContentSize();
    if (GetOwner()) SyncChildObjects(GetOwner());
}

void ScrollComponent::SetItemCount(int32_t count)
{
    m_ItemCount = count;
    CalculateContentSize();
    // Invalidate all slot assignments so they rebind
    std::fill(m_SlotItemIndices.begin(), m_SlotItemIndices.end(), -1);
    if (GetOwner()) SyncChildObjects(GetOwner());
}

void ScrollComponent::SetDirection(ScrollDirection dir)
{
    direction = dir;
}

void ScrollComponent::SetOnBindItem(const ScrollItemCallback& callback)
{
    m_OnBindItem = callback;
    // Invalidate all slot assignments to trigger initial bind
    std::fill(m_SlotItemIndices.begin(), m_SlotItemIndices.end(), -1);
}

void ScrollComponent::ScrollTo(int32_t position, bool smooth)
{
    // Clamp target within valid range
    int32_t maxPos = FixedToInt(GetMaxScrollOffset());
    if (position < 0) position = 0;
    if (position > maxPos) position = maxPos;

    if (smooth)
    {
        m_ScrollTarget = IntToFixed(position);
        m_IsSmoothScrolling = true;
        m_ScrollVelocity = 0;
    }
    else
    {
        m_ScrollOffset = IntToFixed(position);
        m_ScrollVelocity = 0;
        m_IsSmoothScrolling = false;
        if (GetOwner()) SyncChildObjects(GetOwner());
    }
}

void ScrollComponent::ScrollToItem(int32_t index, bool smooth)
{
    if (index < 0 || index >= m_ItemCount) return;

    int32_t position;
    if (mode == ScrollMode::NonTemplate && !m_NonTemplateOffsets.empty())
        position = m_NonTemplateOffsets[index];
    else
        position = index * (m_ItemSize + item_spacing);

    ScrollTo(position, smooth);
}

int32_t ScrollComponent::GetScrollPosition() const
{
    return FixedToInt(m_ScrollOffset);
}

int32_t ScrollComponent::GetSlotCount() const
{
    return static_cast<int32_t>(m_SlotObjs.size());
}

DekiObject* ScrollComponent::GetSlotObject(int32_t slotIndex) const
{
    if (slotIndex >= 0 && slotIndex < static_cast<int32_t>(m_SlotObjs.size()))
        return m_SlotObjs[slotIndex];
    return nullptr;
}

int32_t ScrollComponent::GetSlotItemIndex(int32_t slotIndex) const
{
    if (slotIndex >= 0 && slotIndex < static_cast<int32_t>(m_SlotItemIndices.size()))
        return m_SlotItemIndices[slotIndex];
    return -1;
}

// ============================================================================
// Scroll Physics
// ============================================================================

void ScrollComponent::CalculateContentSize()
{
    //
    if (m_ItemCount <= 0)
    {
        m_ContentSize = 0;
        return;
    }
    int32_t startPad = (direction == ScrollDirection::Vertical) ? padding_top : padding_left;
    int32_t endPad = (direction == ScrollDirection::Vertical) ? padding_bottom : padding_right;

    if (mode == ScrollMode::NonTemplate && !m_NonTemplateOffsets.empty())
    {
        m_ContentSize = startPad + m_NonTemplateOffsets[m_ItemCount] + endPad;
    }
    else
    {
        m_ContentSize = startPad + (m_ItemCount * m_ItemSize) + ((m_ItemCount - 1) * item_spacing) + endPad;
    }
}

int32_t ScrollComponent::GetMaxScrollOffset() const
{
    int32_t vpSize = (direction == ScrollDirection::Vertical) ? GetViewportHeight() : GetViewportWidth();
    int32_t maxScroll = IntToFixed(m_ContentSize - vpSize);
    return (maxScroll < 0) ? 0 : maxScroll;
}

void ScrollComponent::ClampScrollOffset()
{
    
    int32_t vpSize = (direction == ScrollDirection::Vertical) ? GetViewportHeight() : GetViewportWidth();
    int32_t maxScroll = IntToFixed(m_ContentSize - vpSize);
    if (maxScroll < 0) maxScroll = 0;

    if (m_ScrollOffset < 0)
    {
        m_ScrollOffset = 0;
        m_ScrollVelocity = 0;
    }
    else if (m_ScrollOffset > maxScroll)
    {
        m_ScrollOffset = maxScroll;
        m_ScrollVelocity = 0;
    }
}


void ScrollComponent::Update(float delta_time)
{
    if (m_IsDragging) return;

    bool needsSync = false;

    // Phase 1: Smooth scroll animation (from ScrollTo with smooth=true)
    if (m_IsSmoothScrolling)
    {
        int32_t diff = m_ScrollTarget - m_ScrollOffset;
        float easeFactor = 8.0f * 0.15f * delta_time * 60.0f;
        if (easeFactor > 0.5f) easeFactor = 0.5f;

        int32_t step = static_cast<int32_t>(diff * easeFactor);
        if (step == 0 && diff != 0)
            step = (diff > 0) ? 1 : -1;

        m_ScrollOffset += step;

        if (std::abs(m_ScrollTarget - m_ScrollOffset) < 256)
        {
            m_ScrollOffset = m_ScrollTarget;
            m_IsSmoothScrolling = false;
        }

        // Clamp smooth scroll target within bounds
        int32_t maxScroll = GetMaxScrollOffset();
        if (m_ScrollOffset < 0) m_ScrollOffset = 0;
        if (m_ScrollOffset > maxScroll) m_ScrollOffset = maxScroll;

        needsSync = true;
    }
    // Phase 2: Momentum scrolling (after drag release)
    else if (enable_inertia && std::abs(m_ScrollVelocity) > IntToFixed(1))
    {
        m_ScrollOffset += static_cast<int32_t>(m_ScrollVelocity * delta_time * 60.0f);

        // Frame-rate independent deceleration: deceleration^(dt*60)
        float decayFactor = std::pow(deceleration, delta_time * 60.0f);
        m_ScrollVelocity = static_cast<int32_t>(m_ScrollVelocity * decayFactor);

        if (std::abs(m_ScrollVelocity) < IntToFixed(1))
            m_ScrollVelocity = 0;

        needsSync = true;
    }

    // Always clamp within bounds
    ClampScrollOffset();

    if (needsSync && GetOwner())
        SyncChildObjects(GetOwner());
}

void ScrollComponent::HandlePointerDown(float x, float y)
{
    int32_t touchPos = static_cast<int32_t>((direction == ScrollDirection::Vertical) ? y : x);
    m_IsDragging = true;
    m_DragConfirmed = false;
    m_LastTouchPos = touchPos;
    m_TouchStartPos = touchPos;
    m_TouchStartOffset = m_ScrollOffset;
    m_ScrollVelocity = 0;
    m_IsSmoothScrolling = false;
    m_VelSampleIdx = 0;
    m_VelSampleCount = 0;
}

void ScrollComponent::HandlePointerMove(float x, float y)
{
    if (!m_IsDragging) return;

    // If another component claimed the gesture, stop tracking
    if (InputDispatch::IsGestureClaimed() && !InputDispatch::IsGestureClaimedBy(this))
    {
        m_IsDragging = false;
        return;
    }

    int32_t touchPos = static_cast<int32_t>((direction == ScrollDirection::Vertical) ? y : x);

    // Check drag threshold before confirming scroll gesture
    if (!m_DragConfirmed)
    {
        float dist = std::abs(static_cast<float>(touchPos - m_TouchStartPos));
        if (dist < m_DragThreshold)
            return;

        m_DragConfirmed = true;
        InputDispatch::ClaimGesture(this);
        if (m_ClipObj)
            CancelChildInput(m_ClipObj);
    }

    int32_t delta = touchPos - m_LastTouchPos;
    int32_t fixedDelta = IntToFixed(delta);
    int32_t scrollDelta = reverse_drag ? -fixedDelta : fixedDelta;

    m_ScrollOffset += scrollDelta;

    // Store velocity sample in ring buffer
    m_VelSamples[m_VelSampleIdx % kVelocitySamples] = scrollDelta;
    m_VelSampleIdx++;
    if (m_VelSampleCount < kVelocitySamples) m_VelSampleCount++;

    m_LastTouchPos = touchPos;

    // Always clamp — never allow scrolling past limits
    ClampScrollOffset();

    if (GetOwner())
        SyncChildObjects(GetOwner());
}

void ScrollComponent::HandlePointerUp(float x, float y)
{
    (void)x; (void)y;
    if (!m_IsDragging) return;

    m_IsDragging = false;
    m_IsSmoothScrolling = false;

    if (!m_DragConfirmed)
    {
        // Was a tap — don't compute momentum, let children handle click
        return;
    }

    InputDispatch::ReleaseGesture();
    m_DragConfirmed = false;

    // Compute velocity from ring buffer average
    if (m_VelSampleCount > 0)
    {
        int64_t sum = 0;
        int32_t count = std::min(m_VelSampleCount, static_cast<int32_t>(kVelocitySamples));
        for (int32_t i = 0; i < count; i++)
            sum += m_VelSamples[i];
        m_ScrollVelocity = static_cast<int32_t>(sum / count);
    }

    // Kill velocity if already at a bound and velocity would push further
    int32_t maxScroll = GetMaxScrollOffset();
    if ((m_ScrollOffset <= 0 && m_ScrollVelocity < 0) ||
        (m_ScrollOffset >= maxScroll && m_ScrollVelocity > 0))
    {
        m_ScrollVelocity = 0;
    }

    // If velocity is very low, just stop
    if (std::abs(m_ScrollVelocity) < IntToFixed(2))
    {
        m_ScrollVelocity = 0;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void ScrollComponent::Start()
{
    if (GetOwner())
        SyncChildObjects(GetOwner());

    InputCollider* collider = input_collider.Get();
    if (!collider)
    {
        DEKI_LOG_WARNING("ScrollComponent: No InputCollider referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

    // Don't consume input — let children (buttons, nested scrolls) also receive events.
    // Scroll will claim the gesture via InputDispatch when drag threshold is exceeded.
    collider->consume_input = false;

    collider->on_pointer_down.push_back([this](float x, float y) {
        HandlePointerDown(x, y);
    });

    collider->on_pointer_move.push_back([this](float x, float y) {
        HandlePointerMove(x, y);
    });

    collider->on_pointer_up.push_back([this](float x, float y) {
        HandlePointerUp(x, y);
    });
}

bool ScrollComponent::NeedsRuntimeUpdate() const
{
    return true;
}

void ScrollComponent::RuntimeUpdate(float deltaTime)
{
    Update(deltaTime);
}

void ScrollComponent::OnPropertyChanged(const char* propertyName)
{
    static const char* layoutProps[] = {
        "item_spacing", "padding_top", "padding_bottom",
        "padding_left", "padding_right", "direction", "mode"
    };

    for (const char* prop : layoutProps)
    {
        if (std::strcmp(propertyName, prop) == 0)
        {
            if (DekiObject* owner = GetOwner())
            {
                SyncChildObjects(owner);
            }
            return;
        }
    }
}

void ScrollComponent::CancelChildInput(DekiObject* obj)
{
    for (DekiObject* child : obj->GetChildren())
    {
        for (DekiComponent* comp : child->GetComponents())
        {
            if (comp->getType() == InputCollider::StaticType ||
                comp->getBaseType() == InputCollider::StaticType)
            {
                static_cast<InputCollider*>(comp)->CancelInput();
                break;
            }
        }
        CancelChildInput(child);
    }
}
