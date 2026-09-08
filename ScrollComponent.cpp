#include "ScrollComponent.h"
#include "deki-input/InputCollider.h"
#include "deki-input/InputDispatch.h"
#include <deki/Object.h>
#include <deki/LogSystem.h>
#include "ClipComponent.h"
#include "ScrollElement.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include <deki/Scene.h>

// All layout/scroll math here works in world meters — matching the engine's
// meters-internal convention. ScrollElement / ClipComponent / padding values
// are float meters, transform x/y are meters, and pointer callbacks deliver
// meters too. No pixels-per-meter conversion needed anywhere in this file.

ScrollComponent::ScrollComponent()
    : Deki::Component(),
      mode(ScrollMode::NonTemplate),
      direction(ScrollDirection::Vertical),
      itemSpacing(0.0f),
      paddingTop(0.0f),
      paddingBottom(0.0f),
      paddingLeft(0.0f),
      paddingRight(0.0f),
      deceleration(static_cast<float>(0.95f)),
      bounceStiffness(static_cast<float>(0.15f)),
      enableBounce(true),
      enableInertia(true),
      reverseDrag(false),
      m_DragThreshold(static_cast<float>(0.625f))  // ~10 px at ppm=16
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

static Deki::Object* FindChildByName(Deki::Object* parent, const char* name)
{
    if (!parent) return nullptr;
    for (auto* child : parent->GetChildren())
    {
        if (child->GetName() == name)
            return child;
    }
    return nullptr;
}

Deki::Object* ScrollComponent::CloneTemplate(Deki::Object* tmpl, const char* name)
{
    if (!tmpl || !itemScene.Get()) return nullptr;

    Deki::Scene* ownerScene = tmpl->GetOwnerScene();
    if (!ownerScene) return nullptr;

    Deki::Object* instance = itemScene.Get()->Instantiate(ownerScene);
    if (instance)
    {
        instance->SetName(name);
    }
    return instance;
}

float ScrollComponent::MeasureChildSize(Deki::Object* child) const
{
    if (!child) return 0.0f;

    auto* element = child->GetComponent<ScrollElement>();
    if (!element) return 0.0f;

    return (direction == ScrollDirection::Vertical) ? element->height : element->width;
}

int32_t ScrollComponent::GetTotalSlotCount() const
{
    if (m_ItemSize <= 0.0f) return 3;

    float vpSize = (direction == ScrollDirection::Vertical) ? GetViewportHeight() : GetViewportWidth();
    int32_t visible = static_cast<int32_t>(((vpSize) / (m_ItemSize))) + 3;  // +1 top, +1 bottom, +1 rounding
    return std::max(static_cast<int32_t>(3), visible);
}

void ScrollComponent::EnsureChildObjects(Deki::Object* owner)
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
            [](Deki::Object* a, Deki::Object* b) {
                return a->GetName() < b->GetName();
            });

        int32_t totalSlots = GetTotalSlotCount();

        // Create missing slots by cloning template
        while (static_cast<int32_t>(m_SlotObjs.size()) < totalSlots)
        {
            std::string name = "Slot" + std::to_string(m_SlotObjs.size());
            Deki::Object* slot = CloneTemplate(m_TemplateObj, name.c_str());
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
        m_NonTemplateOffsets[0] = 0.0f;
        for (int32_t i = 0; i < m_ItemCount; i++)
        {
            float spacing = (i < m_ItemCount - 1) ? itemSpacing : 0.0f;
            m_NonTemplateOffsets[i + 1] = ((((m_NonTemplateOffsets[i]) + (m_NonTemplateSizes[i]))) + (spacing));
        }

        // Keep m_ItemSize for Template code paths
        m_ItemSize = m_NonTemplateChildren.empty() ? 0.0f : m_NonTemplateSizes[0];
    }
}

bool ScrollComponent::NeedsChildDiscovery(const Deki::Object* owner) const
{
    if (!owner) return true;
    if (!m_ClipObj) return true;
    if (owner->GetChildren().size() != m_DiscoveredOwnerChildren) return true;
    if (m_ClipObj->GetChildren().size() != m_DiscoveredClipChildren) return true;
    return false;
}

void ScrollComponent::SyncChildObjects(Deki::Object* owner)
{
    if (NeedsChildDiscovery(owner))
    {
        EnsureChildObjects(owner);
        m_DiscoveredOwnerChildren = owner ? owner->GetChildren().size() : static_cast<size_t>(-1);
        m_DiscoveredClipChildren = m_ClipObj ? m_ClipObj->GetChildren().size() : static_cast<size_t>(-1);
    }

    CalculateContentSize();
    AssignSlots();
}

// ============================================================================
// Recycling Algorithm
// ============================================================================

void ScrollComponent::AssignSlots()
{
    if (!m_ClipObj) return;

    float scrollPos = m_ScrollOffset;
    float vpSize = (direction == ScrollDirection::Vertical) ? GetViewportHeight() : GetViewportWidth();
    float stride = ((m_ItemSize) + (itemSpacing));
    if (stride <= 0.0f) return;

    float halfVp = ((vpSize) * (0.5f));
    float startPad = (direction == ScrollDirection::Vertical) ? paddingTop : paddingLeft;
    float crossPad = (direction == ScrollDirection::Vertical)
        ? ((((paddingLeft) - (paddingRight))) * (0.5f))
        : ((((paddingTop) - (paddingBottom))) * (0.5f));

    m_FirstVisibleIndex = m_ItemCount;
    m_LastVisibleIndex = -1;

    if (mode == ScrollMode::Template)
    {
        if (m_SlotObjs.empty() || m_ItemCount <= 0) return;

        // First item index to show (with 1 buffer before viewport).
        float adjustedScroll = ((scrollPos) - (startPad));
        int32_t firstIndex = static_cast<int32_t>(std::floor(((adjustedScroll) / (stride)))) - 1;
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

            // Position: item meter offset relative to viewport center.
            float pos = ((((startPad) + (((static_cast<float>(itemIndex)) * (stride))))) - (scrollPos));
            float slotCenter = ((((pos) + (((m_ItemSize) * (0.5f))))) - (halfVp));

            if (direction == ScrollDirection::Vertical)
                m_SlotObjs[s]->SetLocalPosition(crossPad, ((0.0f) - (slotCenter)));
            else
                m_SlotObjs[s]->SetLocalPosition(slotCenter, crossPad);

            // Track visible range
            if (((pos) + (m_ItemSize)) > 0.0f && pos < vpSize)
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
            Deki::Object* child = m_NonTemplateChildren[i];
            if (!child) continue;

            float itemSize = m_NonTemplateSizes[i];
            float pos = ((((startPad) + (m_NonTemplateOffsets[i]))) - (scrollPos));

            // Visibility culling with per-item size buffer
            float negItemSize = ((0.0f) - (itemSize));
            float vpPlusItem  = ((vpSize) + (itemSize));
            bool visible = (((pos) + (itemSize)) > negItemSize && pos < vpPlusItem);
            child->SetActive(visible);

            if (visible)
            {
                float center = ((((pos) + (((itemSize) * (0.5f))))) - (halfVp));

                if (direction == ScrollDirection::Vertical)
                    child->SetLocalPosition(crossPad, ((0.0f) - (center)));
                else
                    child->SetLocalPosition(center, crossPad);
            }

            // Track visible range (within actual viewport, not buffer)
            if (((pos) + (itemSize)) > 0.0f && pos < vpSize)
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

float ScrollComponent::GetViewportWidth() const
{
    if (m_ClipObj)
    {
        if (auto* clip = m_ClipObj->GetComponent<ClipComponent>())
            return clip->width;
    }
    return 0.0f;
}

float ScrollComponent::GetViewportHeight() const
{
    if (m_ClipObj)
    {
        if (auto* clip = m_ClipObj->GetComponent<ClipComponent>())
            return clip->height;
    }
    return 0.0f;
}

void ScrollComponent::SetItemSpacing(float spacing)
{
    itemSpacing = spacing;
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

void ScrollComponent::ScrollTo(float position, bool smooth)
{
    // Clamp target within valid range
    float maxPos = GetMaxScrollOffset();
    if (position < 0.0f) position = 0.0f;
    if (position > maxPos) position = maxPos;

    if (smooth)
    {
        m_ScrollTarget = position;
        m_IsSmoothScrolling = true;
        m_ScrollVelocity = 0.0f;
    }
    else
    {
        m_ScrollOffset = position;
        m_ScrollVelocity = 0.0f;
        m_IsSmoothScrolling = false;
        if (GetOwner()) SyncChildObjects(GetOwner());
    }
}

void ScrollComponent::ScrollToItem(int32_t index, bool smooth)
{
    if (index < 0 || index >= m_ItemCount) return;

    float position;
    if (mode == ScrollMode::NonTemplate && !m_NonTemplateOffsets.empty())
        position = m_NonTemplateOffsets[index];
    else
        position = ((static_cast<float>(index)) * (((m_ItemSize) + (itemSpacing))));

    ScrollTo(position, smooth);
}

float ScrollComponent::GetScrollPosition() const
{
    return m_ScrollOffset;
}

int32_t ScrollComponent::GetSlotCount() const
{
    return static_cast<int32_t>(m_SlotObjs.size());
}

Deki::Object* ScrollComponent::GetSlotObject(int32_t slotIndex) const
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
    if (m_ItemCount <= 0)
    {
        m_ContentSize = 0.0f;
        return;
    }
    float startPad = (direction == ScrollDirection::Vertical) ? paddingTop : paddingLeft;
    float endPad   = (direction == ScrollDirection::Vertical) ? paddingBottom : paddingRight;

    if (mode == ScrollMode::NonTemplate && !m_NonTemplateOffsets.empty())
    {
        m_ContentSize = ((((startPad) + (m_NonTemplateOffsets[m_ItemCount]))) + (endPad));
    }
    else
    {
        // startPad + N*itemSize + (N-1)*spacing + endPad, all float.
        float items  = ((static_cast<float>(m_ItemCount)) * (m_ItemSize));
        float gaps   = ((static_cast<float>(m_ItemCount - 1)) * (itemSpacing));
        m_ContentSize = ((((((startPad) + (items))) + (gaps))) + (endPad));
    }
}

float ScrollComponent::GetMaxScrollOffset() const
{
    float vpSize = (direction == ScrollDirection::Vertical) ? GetViewportHeight() : GetViewportWidth();
    float maxScroll = ((m_ContentSize) - (vpSize));
    return (maxScroll < 0.0f) ? 0.0f : maxScroll;
}

void ScrollComponent::ClampScrollOffset()
{
    float maxScroll = GetMaxScrollOffset();

    if (m_ScrollOffset < 0.0f)
    {
        m_ScrollOffset = 0.0f;
        m_ScrollVelocity = 0.0f;
    }
    else if (m_ScrollOffset > maxScroll)
    {
        m_ScrollOffset = maxScroll;
        m_ScrollVelocity = 0.0f;
    }
}


void ScrollComponent::Update(float delta_time)
{
    if (m_IsDragging) return;

    bool needsSync = false;

    // Thresholds in meters/frame, sized to preserve the prior pixel feel at
    // ppm=16. Computed once at first call so the macro arguments evaluate in
    // their proper mode.
    static const float kVelocityEpsilon    = static_cast<float>(0.0625f);  // ~1 px/frame
    static const float kSmoothArriveEpsilon = static_cast<float>(0.004f);  // ~0.06 px

    // Phase 1: Smooth scroll animation (from ScrollTo with smooth=true)
    if (m_IsSmoothScrolling)
    {
        float diff = m_ScrollTarget - m_ScrollOffset;
        float easeFactor = 8.0f * 0.15f * delta_time * 60.0f;
        if (easeFactor > 0.5f) easeFactor = 0.5f;

        m_ScrollOffset = m_ScrollOffset + diff * easeFactor;

        if (Deki::Math::Abs(((m_ScrollTarget) - (m_ScrollOffset))) < kSmoothArriveEpsilon)
        {
            m_ScrollOffset = m_ScrollTarget;
            m_IsSmoothScrolling = false;
        }

        // Clamp smooth scroll target within bounds
        float maxScroll = GetMaxScrollOffset();
        if (m_ScrollOffset < 0.0f) m_ScrollOffset = 0.0f;
        if (m_ScrollOffset > maxScroll) m_ScrollOffset = maxScroll;

        needsSync = true;
    }
    // Phase 2: Momentum scrolling (after drag release)
    else if (enableInertia && Deki::Math::Abs(m_ScrollVelocity) > kVelocityEpsilon)
    {
        float stepCoef = static_cast<float>(delta_time * 60.0f);
        m_ScrollOffset = ((m_ScrollOffset) + (((m_ScrollVelocity) * (stepCoef))));

        // Frame-rate independent deceleration: deceleration^(dt*60)
        float decayFactor = std::pow((deceleration), (stepCoef));
        m_ScrollVelocity = ((m_ScrollVelocity) * (decayFactor));

        if (Deki::Math::Abs(m_ScrollVelocity) < kVelocityEpsilon)
            m_ScrollVelocity = 0.0f;

        needsSync = true;
    }

    // Always clamp within bounds
    ClampScrollOffset();

    if (needsSync && GetOwner())
        SyncChildObjects(GetOwner());
}

void ScrollComponent::HandlePointerDown(float x, float y)
{
    // Pointer x/y are world meters as float (input FFI). Convert to float
    // at the boundary so internal touch tracking lives in the same numeric
    // space as scroll state.
    float touchPos = static_cast<float>((direction == ScrollDirection::Vertical) ? y : x);
    m_IsDragging = true;
    m_DragConfirmed = false;
    m_LastTouchPos = touchPos;
    m_TouchStartPos = touchPos;
    m_TouchStartOffset = m_ScrollOffset;
    m_ScrollVelocity = 0.0f;
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

    float touchPos = static_cast<float>((direction == ScrollDirection::Vertical) ? y : x);

    // Check drag threshold before confirming scroll gesture
    if (!m_DragConfirmed)
    {
        float dist = Deki::Math::Abs(((touchPos) - (m_TouchStartPos)));
        if (dist < m_DragThreshold)
            return;

        m_DragConfirmed = true;
        InputDispatch::ClaimGesture(this);
        if (m_ClipObj)
            CancelChildInput(m_ClipObj);
    }

    float delta = ((touchPos) - (m_LastTouchPos));
    float scrollDelta = reverseDrag ? ((0.0f) - (delta)) : delta;

    m_ScrollOffset = ((m_ScrollOffset) + (scrollDelta));

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

    // Compute velocity from ring buffer average (sum in float then back to float).
    if (m_VelSampleCount > 0)
    {
        double sum = 0.0;
        int32_t count = std::min(m_VelSampleCount, static_cast<int32_t>(kVelocitySamples));
        for (int32_t i = 0; i < count; i++)
            sum += (m_VelSamples[i]);
        m_ScrollVelocity = static_cast<float>(sum / count);
    }

    // Kill velocity if already at a bound and velocity would push further
    float maxScroll = GetMaxScrollOffset();
    if ((m_ScrollOffset <= 0.0f && m_ScrollVelocity < 0.0f) ||
        (m_ScrollOffset >= maxScroll && m_ScrollVelocity > 0.0f))
    {
        m_ScrollVelocity = 0.0f;
    }

    // Velocity-very-low threshold: ~2 px/frame at ppm=16
    static const float kStopThreshold = static_cast<float>(0.125f);
    if (Deki::Math::Abs(m_ScrollVelocity) < kStopThreshold)
    {
        m_ScrollVelocity = 0.0f;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void ScrollComponent::Start()
{
    if (GetOwner())
        SyncChildObjects(GetOwner());

    InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        DEKI_LOG_WARNING("ScrollComponent: No InputCollider referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

    // Don't consume input — let children (buttons, nested scrolls) also receive events.
    // Scroll will claim the gesture via InputDispatch when drag threshold is exceeded.
    collider->consumeInput = false;

    collider->onPointerDown.push_back([this](float x, float y) {
        HandlePointerDown(x, y);
    });

    collider->onPointerMove.push_back([this](float x, float y) {
        HandlePointerMove(x, y);
    });

    collider->onPointerUp.push_back([this](float x, float y) {
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
        "itemSpacing", "paddingTop", "paddingBottom",
        "paddingLeft", "paddingRight", "direction", "mode"
    };

    for (const char* prop : layoutProps)
    {
        if (std::strcmp(propertyName, prop) == 0)
        {
            if (Deki::Object* owner = GetOwner())
            {
                SyncChildObjects(owner);
            }
            return;
        }
    }
}

void ScrollComponent::CancelChildInput(Deki::Object* obj)
{
    for (Deki::Object* child : obj->GetChildren())
    {
        for (Deki::Component* comp : child->GetComponents())
        {
            if (comp->GetType() == ::Deki::TypeId<InputCollider>() ||
                comp->GetBaseType() == ::Deki::TypeId<InputCollider>())
            {
                static_cast<InputCollider*>(comp)->CancelInput();
                break;
            }
        }
        CancelChildInput(child);
    }
}
