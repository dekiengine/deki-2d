#include "RollerComponent.h"
#include <cstdlib>
#include "deki-input/InputCollider.h"
#include "TextComponent.h"
#include "SpriteComponent.h"
#include <deki/Object.h>
#ifdef DEKI_EDITOR
#include <deki-editor/EditorComponents.h>
#endif
#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include "deki-rendering/CameraComponent.h"
#include "ClipComponent.h"
#include "deki-rendering/QuadBlit.h"
#include "BitmapFont.h"
#include "Texture2D.h"
#include "Sprite.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

// All sizes/positions in this file are world meters (matches the engine's
// meters-internal convention; transform x/y and pointer callbacks are
// meters too). No pixels-per-meter conversions needed.

// ============================================================================
// Component Registration
// ============================================================================
// NOTE: s_Properties[] and s_ComponentMeta are now auto-generated in
// RollerComponent.gen.h (included at end of RollerComponent.h)


RollerComponent::RollerComponent()
    : Deki::Behaviour(),
      width(static_cast<float>(6.25f)),
      itemHeight(static_cast<float>(1.875f)),
      visibleRows(3),
      infiniteScroll(false),
      reverseDrag(false),
      selectedIndex(0),
      deceleration(static_cast<float>(0.95f)),
      snapSpeed(static_cast<float>(8.0f)),
      selectedItemHeight(static_cast<float>(2.5f)),
      selectedColor(255, 255, 255),
      normalColor(128, 128, 128),
#ifdef DEKI_EDITOR
      selectedFontSize(24),
      normalFontSize(16),
#endif
      m_ScrollOffset(0.0f),
      m_ScrollVelocity(0.0f),
      m_TargetOffset(0.0f),
      m_IsSnapping(false),
      m_NeedsSync(false),
      m_IsDragging(false),
      m_LastTouchY(0.0f),
      m_TouchStartY(0.0f),
      m_TouchStartOffset(0.0f),
      m_LastSyncedSelectedItemHeight(-1.0f),
      m_LastSyncedItemHeight(-1.0f)
{
}

RollerComponent::~RollerComponent()
{
    // Child objects are owned by the parent Deki::Object, not by us
    // Just clear our references
    m_ClipObj = nullptr;
    m_BackgroundObj = nullptr;
    m_SelectionObj = nullptr;
    m_TextRowObjs.clear();
}

Deki::Object* RollerComponent::FindOrCreateChild(Deki::Object* owner, const char* name, const char* componentType)
{
    if (!owner) return nullptr;

    // Search existing children by name
    for (auto* child : owner->GetChildren())
    {
        if (child->GetName() == name)
            return child;
    }

#ifdef DEKI_EDITOR
    // Create new child object (editor only - runtime loads from scene)
    Deki::Object* child = new Deki::Object(name);
    owner->AddChild(child);
    DekiEditor::AddComponentByName(child, componentType);
    return child;
#else
    // Runtime: child should exist from scene loading
    return nullptr;
#endif
}

void RollerComponent::UpdateTextRowCount(Deki::Object* owner)
{
    if (!owner) return;

    // Find existing text rows
    m_TextRowObjs.clear();
    for (auto* child : owner->GetChildren())
    {
        if (child->GetName().rfind("TextRow", 0) == 0)
        {
            child->SetActive(true);  // Re-enable in case it was hidden
            m_TextRowObjs.push_back(child);
        }
    }

    // Sort by the numeric suffix. Lexicographic order put TextRow10 before
    // TextRow2 once a roller had more than ten rows.
    auto rowNumber = [](const Deki::Object* o) -> long
    {
        const std::string& n = o->GetName();
        return std::strtol(n.c_str() + 7, nullptr, 10);  // after "TextRow"
    };
    std::sort(m_TextRowObjs.begin(), m_TextRowObjs.end(),
        [&rowNumber](Deki::Object* a, Deki::Object* b) {
            return rowNumber(a) < rowNumber(b);
        });

    // Create missing rows (visibleRows + 2 for smooth scrolling: 1 extra top + 1 extra bottom)
    int32_t totalRows = visibleRows + 2;

    // Find a template text component to copy font settings from
    TextComponent* templateTC = nullptr;
    for (auto* existingRow : m_TextRowObjs)
    {
        if (existingRow)
        {
            if (auto* tc = existingRow->GetComponent<TextComponent>())
            {
                if (!tc->font.guid.empty() || !tc->font.source.empty())
                {
                    templateTC = tc;
                    break;
                }
            }
        }
    }

    while (static_cast<int32_t>(m_TextRowObjs.size()) < totalRows)
    {
        std::string name = "TextRow" + std::to_string(m_TextRowObjs.size());
        Deki::Object* row = FindOrCreateChild(owner, name.c_str(), "TextComponent");
        if (row)
        {
            // Set default text component properties
            if (auto* tc = row->GetComponent<TextComponent>())
            {
                tc->align = TextAlign::Center;
                tc->verticalAlign = TextVerticalAlign::Middle;

                // Copy font settings from template if available
                if (templateTC)
                {
                    tc->font = templateTC->font;
#ifdef DEKI_EDITOR
                    tc->fontSize = templateTC->fontSize;
#endif
                    tc->color = templateTC->color;
                }
            }
            m_TextRowObjs.push_back(row);
        }
    }

    // Hide excess rows (don't delete - user may have customized them)
    for (size_t i = totalRows; i < m_TextRowObjs.size(); ++i)
    {
        m_TextRowObjs[i]->SetActive(false);
    }
    // Trim to needed count
    if (m_TextRowObjs.size() > static_cast<size_t>(totalRows))
    {
        m_TextRowObjs.resize(totalRows);
    }
}

void RollerComponent::EnsureChildObjects(Deki::Object* owner)
{
    if (!owner) return;

    // Find or create Clip child on owner
    m_ClipObj = FindOrCreateChild(owner, "Clip", "ClipComponent");

    // All other children go inside the Clip object (so they get clipped)
    if (m_ClipObj)
    {
        // Find or create Background child inside Clip
        m_BackgroundObj = FindOrCreateChild(m_ClipObj, "Background", "SpriteComponent");

        // Find or create Selection child inside Clip
        m_SelectionObj = FindOrCreateChild(m_ClipObj, "Selection", "SpriteComponent");

        // Update text row children inside Clip
        UpdateTextRowCount(m_ClipObj);
    }
}

bool RollerComponent::NeedsChildDiscovery(const Deki::Object* owner) const
{
    if (!owner) return true;
    if (!m_ClipObj) return true;
    if (owner->GetChildren().size() != m_DiscoveredOwnerChildren) return true;
    if (m_ClipObj->GetChildren().size() != m_DiscoveredClipChildren) return true;
    if (m_TextRowObjs.size() != static_cast<size_t>(visibleRows + 2)) return true;
    return false;
}

void RollerComponent::SyncChildObjects(Deki::Object* owner)
{
    // Discovery (three name scans, a sort and a text copy per row) used to
    // run on every pointer move and every animation frame.
    if (NeedsChildDiscovery(owner))
    {
        EnsureChildObjects(owner);
        m_DiscoveredOwnerChildren = owner ? owner->GetChildren().size() : static_cast<size_t>(-1);
        m_DiscoveredClipChildren = m_ClipObj ? m_ClipObj->GetChildren().size() : static_cast<size_t>(-1);
    }

    // Sync scroll offset when selectedIndex or height properties change externally (e.g., from inspector)
    // Only do this when not actively scrolling
    bool indexChanged = (selectedIndex != m_LastSyncedSelectedIndex);
    bool heightsChanged = (selectedItemHeight != m_LastSyncedSelectedItemHeight ||
                           itemHeight != m_LastSyncedItemHeight);

    if (!m_IsDragging && !m_IsSnapping && (indexChanged || heightsChanged))
    {
        m_ScrollOffset = GetItemOffset(selectedIndex);
        m_LastSyncedSelectedIndex = selectedIndex;
        m_LastSyncedSelectedItemHeight = selectedItemHeight;
        m_LastSyncedItemHeight = itemHeight;
    }

    int32_t centerRow = visibleRows / 2;

    // Sync clip child position (user controls ClipComponent width/height directly)
    if (m_ClipObj)
    {
        m_ClipObj->SetLocalPosition(0.0f, 0.0f);
    }

    // Sync background child position (sprite sizing controlled by user via texture or scale)
    if (m_BackgroundObj)
    {
        m_BackgroundObj->SetLocalPosition(0.0f, 0.0f);
    }

    // Sync selection child position (at center row, Y=0 in center-based coordinates)
    if (m_SelectionObj)
    {
        m_SelectionObj->SetLocalPosition(0.0f, 0.0f);
    }

    // Sync text row children
    // Get the item index that should be shown at the center based on scroll offset
    float scrollOffset = m_ScrollOffset;
    int32_t rawCenteredIndex = GetRawIndexAtOffset(scrollOffset);

    // Use raw (unwrapped) index for offset calculation so subItemOffset stays small
    float snapOffset = GetItemOffset(rawCenteredIndex);
    float subItemOffset = ((scrollOffset) - (snapOffset));

    // We have visibleRows + 2 text rows for smooth scrolling
    // Row 0 is 1 position above top visible, last row is 1 below bottom visible
    int32_t totalRows = visibleRows + 2;

    for (int32_t row = 0; row < static_cast<int32_t>(m_TextRowObjs.size()) && row < totalRows; row++)
    {
        Deki::Object* textObj = m_TextRowObjs[row];
        if (!textObj) continue;

        // Virtual row: row 0 maps to position -1 (above top), row 1 to position 0 (top visible), etc.
        int32_t virtualRow = row - 1;

        // Calculate item index for this row (used for text and styling)
        // Use raw (unwrapped) centered index so offsets stay in the same domain
        int32_t itemIndex = rawCenteredIndex + (virtualRow - centerRow);
        bool validIndex = true;

        if (infiniteScroll)
        {
            int32_t optSize = static_cast<int32_t>(options.size());
            if (optSize > 0)
                itemIndex = ((itemIndex % optSize) + optSize) % optSize;
        }
        else
        {
            validIndex = (itemIndex >= 0 && itemIndex < static_cast<int32_t>(options.size()));
        }

        // Determine if this is the center row (always gets selected styling for consistent layout)
        bool isCenterRow = (virtualRow == centerRow);
        float rowHeight = isCenterRow ? selectedItemHeight : itemHeight;

        if (auto* tc = textObj->GetComponent<TextComponent>())
        {
            // Sync size - use different height for selected row.
            tc->width = width;
            tc->height = rowHeight;

            // Apply color based on row position (center row gets selected styling)
            tc->color = isCenterRow ? selectedColor : normalColor;

#ifdef DEKI_EDITOR
            // Apply font size based on row position (for sharp text rendering)
            tc->fontSize = isCenterRow ? selectedFontSize : normalFontSize;
#endif

            // Set text from options
            if (!options.empty())
            {
                if (infiniteScroll || validIndex)
                {
                    tc->text = options[itemIndex];
                }
                else
                {
                    // Clear text when out of range (non-infinite scroll)
                    tc->text.clear();
                }
            }
        }

        // Position text rows from edges of center row.
        // Center row ALWAYS stays at Y=0, only non-center rows move with scroll.
        float rowY = 0.0f;
        int32_t rowDist = centerRow - virtualRow;  // positive = above center, negative = below

        if (rowDist > 0)
        {
            // Above center: first row's bottom touches center's top
            float bottomEdge = ((selectedItemHeight) * (0.5f));
            bottomEdge = ((bottomEdge) + (((static_cast<float>(rowDist - 1)) * (itemHeight))));
            rowY = ((((bottomEdge) + (((rowHeight) * (0.5f))))) + (subItemOffset));
        }
        else if (rowDist < 0)
        {
            // Below center: first row's top touches center's bottom
            float topEdge = ((0.0f) - (((selectedItemHeight) * (0.5f))));
            topEdge = ((topEdge) - (((static_cast<float>(-rowDist - 1)) * (itemHeight))));
            rowY = ((((topEdge) - (((rowHeight) * (0.5f))))) + (subItemOffset));
        }
        // else rowDist == 0 (center row): rowY stays 0, no subItemOffset applied

        textObj->SetLocalPosition(0.0f, rowY);
        // Don't force active - respect user's active state
    }
}

// Accessors for editor rendering (get components from child objects)
SpriteComponent* RollerComponent::GetBackgroundSprite()
{
    if (m_BackgroundObj)
        return m_BackgroundObj->GetComponent<SpriteComponent>();
    return nullptr;
}

SpriteComponent* RollerComponent::GetSelectionSprite()
{
    if (m_SelectionObj)
        return m_SelectionObj->GetComponent<SpriteComponent>();
    return nullptr;
}

TextComponent* RollerComponent::GetTextComponent(int32_t row)
{
    if (row >= 0 && row < static_cast<int32_t>(m_TextRowObjs.size()))
    {
        if (m_TextRowObjs[row])
            return m_TextRowObjs[row]->GetComponent<TextComponent>();
    }
    return nullptr;
}

void RollerComponent::SetOptions(const std::vector<std::string>& newOptions)
{
    options = newOptions;

    // Reset selection if out of bounds
    if (selectedIndex >= static_cast<int32_t>(options.size()))
    {
        selectedIndex = options.empty() ? 0 : static_cast<int32_t>(options.size()) - 1;
    }

    // Reset scroll to selected item
    m_ScrollOffset = GetItemOffset(selectedIndex);
    m_ScrollVelocity = 0.0f;
    m_IsSnapping = false;
    m_NeedsSync = true;
    if (GetOwner())
        SyncChildObjects(GetOwner());
}

std::string RollerComponent::GetSelectedValue() const
{
    if (selectedIndex >= 0 && selectedIndex < static_cast<int32_t>(options.size()))
    {
        return options[selectedIndex];
    }
    return "";
}

void RollerComponent::SetSelectedIndex(int32_t index, bool animated)
{
    if (options.empty())
        return;

    // Clamp index
    if (infiniteScroll)
    {
        index = ((index % static_cast<int32_t>(options.size())) + static_cast<int32_t>(options.size())) % static_cast<int32_t>(options.size());
    }
    else
    {
        index = std::max(static_cast<int32_t>(0), std::min(index, static_cast<int32_t>(options.size()) - 1));
    }

    int32_t oldIndex = selectedIndex;
    selectedIndex = index;
    m_LastSyncedSelectedIndex = index;  // Track programmatic change

    if (animated)
    {
        m_TargetOffset = GetItemOffset(index);
        m_IsSnapping = true;
        m_ScrollVelocity = 0.0f;
    }
    else
    {
        m_ScrollOffset = GetItemOffset(index);
        m_ScrollVelocity = 0.0f;
        m_IsSnapping = false;
        m_NeedsSync = true;
    }

    // Fire callback if selection changed
    if (oldIndex != selectedIndex && m_OnSelectionChanged)
    {
        m_OnSelectionChanged(selectedIndex, GetSelectedValue());
    }

    if (m_NeedsSync && GetOwner())
    {
        SyncChildObjects(GetOwner());
        m_NeedsSync = false;
    }
}

void RollerComponent::SetVisibleRowCount(int32_t rows)
{
    // Ensure odd number for centered selection
    visibleRows = std::max(static_cast<int32_t>(1), rows);
    if (visibleRows % 2 == 0)
    {
        visibleRows++;
    }
}

void RollerComponent::SetOnSelectionChanged(const RollerCallback& callback)
{
    m_OnSelectionChanged = callback;
}

void RollerComponent::SetOnValueCommitted(const RollerCallback& callback)
{
    m_OnValueCommitted = callback;
}

float RollerComponent::GetItemOffset(int32_t index) const
{
    // Center the selected item in the visible area.
    // The center row has selectedItemHeight, other rows have itemHeight.
    // Distance from center to adjacent item = half of each height.
    int32_t centerRow = visibleRows / 2;
    int32_t dist = index - centerRow;

    if (dist == 0) return 0.0f;

    // First step from center uses the transition distance (half of each height).
    // Subsequent steps use uniform itemHeight.
    float transitionDist = ((((selectedItemHeight) + (itemHeight))) * (0.5f));

    if (dist > 0)
    {
        return ((transitionDist) + (((static_cast<float>(dist - 1)) * (itemHeight))));
    }
    else
    {
        float pos = ((transitionDist) + (((static_cast<float>(-dist - 1)) * (itemHeight))));
        return ((0.0f) - (pos));
    }
}

int32_t RollerComponent::GetIndexAtOffset(float offset) const
{
    if (options.empty())
        return 0;

    // Calculate which item is closest to the center selection area.
    // An item should be selected when more than 50% of it is in the center row;
    // this matches the snap behavior - the item that will be snapped to should
    // be highlighted.

    int32_t centerRow = visibleRows / 2;
    float transitionDist = ((((selectedItemHeight) + (itemHeight))) * (0.5f));
    float halfTransition = ((transitionDist) * (0.5f));  // 50% threshold for first item

    int32_t index;
    float absOffset = Deki::Math::Abs(offset);

    if (absOffset < halfTransition)
    {
        index = centerRow;
    }
    else if (offset > 0.0f)
    {
        if (absOffset < transitionDist)
        {
            index = centerRow + 1;
        }
        else
        {
            float pastFirst = ((absOffset) - (transitionDist));
            float adjusted = ((pastFirst) + (((itemHeight) * (0.5f))));
            int32_t additionalItems = static_cast<int32_t>(std::floor(((adjusted) / (itemHeight))));
            index = centerRow + 1 + additionalItems;
        }
    }
    else
    {
        if (absOffset < transitionDist)
        {
            index = centerRow - 1;
        }
        else
        {
            float pastFirst = ((absOffset) - (transitionDist));
            float adjusted = ((pastFirst) + (((itemHeight) * (0.5f))));
            int32_t additionalItems = static_cast<int32_t>(std::floor(((adjusted) / (itemHeight))));
            index = centerRow - 1 - additionalItems;
        }
    }

    if (infiniteScroll)
    {
        index = ((index % static_cast<int32_t>(options.size())) + static_cast<int32_t>(options.size())) % static_cast<int32_t>(options.size());
    }
    else
    {
        index = std::max(static_cast<int32_t>(0), std::min(index, static_cast<int32_t>(options.size()) - 1));
    }

    return index;
}

int32_t RollerComponent::GetRawIndexAtOffset(float offset) const
{
    if (options.empty())
        return 0;

    int32_t centerRow = visibleRows / 2;
    float transitionDist = ((((selectedItemHeight) + (itemHeight))) * (0.5f));
    float halfTransition = ((transitionDist) * (0.5f));

    int32_t index;
    float absOffset = Deki::Math::Abs(offset);

    if (absOffset < halfTransition)
    {
        index = centerRow;
    }
    else if (offset > 0.0f)
    {
        if (absOffset < transitionDist)
        {
            index = centerRow + 1;
        }
        else
        {
            float pastFirst = ((absOffset) - (transitionDist));
            float adjusted = ((pastFirst) + (((itemHeight) * (0.5f))));
            int32_t additionalItems = static_cast<int32_t>(std::floor(((adjusted) / (itemHeight))));
            index = centerRow + 1 + additionalItems;
        }
    }
    else
    {
        if (absOffset < transitionDist)
        {
            index = centerRow - 1;
        }
        else
        {
            float pastFirst = ((absOffset) - (transitionDist));
            float adjusted = ((pastFirst) + (((itemHeight) * (0.5f))));
            int32_t additionalItems = static_cast<int32_t>(std::floor(((adjusted) / (itemHeight))));
            index = centerRow - 1 - additionalItems;
        }
    }

    // No wrapping — return raw index
    return index;
}

void RollerComponent::ClampScrollOffset()
{
    if (options.empty() || infiniteScroll)
        return;

    float minOffset = GetItemOffset(0);
    float maxOffset = GetItemOffset(static_cast<int32_t>(options.size()) - 1);

    if (m_ScrollOffset < minOffset)
    {
        m_ScrollOffset = minOffset;
        m_ScrollVelocity = 0.0f;
    }
    else if (m_ScrollOffset > maxOffset)
    {
        m_ScrollOffset = maxOffset;
        m_ScrollVelocity = 0.0f;
    }
}

void RollerComponent::SnapToNearestItem()
{
    if (options.empty())
        return;

    int32_t rawIndex = GetRawIndexAtOffset(m_ScrollOffset);

    // Use raw (unwrapped) index so target is in the same domain as m_ScrollOffset
    m_TargetOffset = GetItemOffset(rawIndex);
    m_IsSnapping = true;
}

void RollerComponent::UpdateSelection()
{
    if (options.empty())
        return;

    int32_t rawIndex = GetRawIndexAtOffset(m_ScrollOffset);

    // Wrap for actual selection
    int32_t newIndex;
    if (infiniteScroll)
    {
        int32_t optSize = static_cast<int32_t>(options.size());
        newIndex = ((rawIndex % optSize) + optSize) % optSize;
    }
    else
    {
        newIndex = std::max(static_cast<int32_t>(0), std::min(rawIndex, static_cast<int32_t>(options.size()) - 1));
    }

    if (newIndex != selectedIndex)
    {
        selectedIndex = newIndex;
        m_LastSyncedSelectedIndex = newIndex;  // Track internal change

        if (m_OnSelectionChanged)
        {
            m_OnSelectionChanged(selectedIndex, GetSelectedValue());
        }
    }
}

void RollerComponent::HandlePointerDown(float x, float y)
{
    (void)x;
    // Pointer y arrives in world meters as float (input FFI); convert to
    // float at the boundary to stay in the same numeric space as state.
    float touchY = static_cast<float>(y);
    m_IsDragging = true;
    m_LastTouchY = touchY;
    m_TouchStartY = touchY;
    m_TouchStartOffset = m_ScrollOffset;
    m_ScrollVelocity = 0.0f;
    m_IsSnapping = false;
}

void RollerComponent::HandlePointerMove(float x, float y)
{
    (void)x;
    if (!m_IsDragging) return;

    float touchY = static_cast<float>(y);
    float delta = ((touchY) - (m_LastTouchY));

    // Default: drag follows touch direction (like ScrollComponent)
    // reverseDrag: drag moves opposite to touch
    float scrollDelta = reverseDrag ? ((0.0f) - (delta)) : delta;
    m_ScrollOffset = ((m_ScrollOffset) + (scrollDelta));

    // Track velocity for momentum (reduced for less aggressive scrolling)
    static const float kThird = static_cast<float>(1.0f / 3.0f);
    m_ScrollVelocity = ((scrollDelta) * (kThird));

    m_LastTouchY = touchY;

    ClampScrollOffset();
    UpdateSelection();

    // Update visual positions of text rows
    if (GetOwner())
        SyncChildObjects(GetOwner());
}

void RollerComponent::HandlePointerUp(float x, float y)
{
    (void)x;
    if (!m_IsDragging) return;

    m_IsDragging = false;

    float touchY = static_cast<float>(y);

    // Tap-vs-drag threshold: one-third of an item.
    static const float kThird = static_cast<float>(1.0f / 3.0f);
    float dragDistance = Deki::Math::Abs(((touchY) - (m_TouchStartY)));
    float tapThreshold = ((itemHeight) * (kThird));
    if (dragDistance < tapThreshold)
    {
        Deki::Object* owner = GetOwner();
        if (owner)
        {
            // Calculate which row was tapped — everything in meters.
            float centerY = owner->GetWorldY();
            float rollerTop = ((centerY) - (((GetHeight()) * (0.5f))));
            float relativeY = ((touchY) - (rollerTop));
            int32_t tappedRow = static_cast<int32_t>(std::floor(((relativeY) / (itemHeight))));

            // Clamp to valid row range
            if (tappedRow >= 0 && tappedRow < visibleRows)
            {
                int32_t centerRow = visibleRows / 2;
                int32_t rowOffset = tappedRow - centerRow;

                if (rowOffset != 0)
                {
                    // Calculate the target index
                    int32_t targetIndex = selectedIndex + rowOffset;

                    // Clamp or wrap based on infiniteScroll
                    if (infiniteScroll)
                    {
                        int32_t optSize = static_cast<int32_t>(options.size());
                        targetIndex = ((targetIndex % optSize) + optSize) % optSize;
                    }
                    else
                    {
                        targetIndex = std::max(static_cast<int32_t>(0), std::min(targetIndex, static_cast<int32_t>(options.size()) - 1));
                    }

                    // Animate to the tapped item
                    if (targetIndex != selectedIndex)
                    {
                        SetSelectedIndex(targetIndex, true);
                        return;
                    }
                }
            }
        }

        // If tapped center row or invalid, just snap
        m_ScrollVelocity = 0.0f;
        SnapToNearestItem();
        return;
    }

    // If velocity is low (slow drag or stopped between items), snap immediately.
    // Threshold: ~3 px/frame at ppm=16.
    static const float kLowVelocity = static_cast<float>(0.1875f);
    if (Deki::Math::Abs(m_ScrollVelocity) < kLowVelocity)
    {
        m_ScrollVelocity = 0.0f;
        SnapToNearestItem();
    }
    // Otherwise let momentum continue, Update() will snap when velocity dies down
}

void RollerComponent::Update(float delta_time)
{
    if (m_IsDragging)
        return;

    bool needsSync = false;

    // Velocity thresholds in meters/frame (preserve prior px-based feel at ppm=16):
    static const float kVelocityActive = static_cast<float>(0.0625f);  // > 1 px/frame
    static const float kVelocityCutoff = static_cast<float>(0.125f);   // < 2 px/frame triggers snap
    static const float kSnapArrived    = static_cast<float>(0.004f);   // < ~0.06 px from target

    // Phase 1: Momentum scrolling (after release, before snap)
    if (!m_IsSnapping && Deki::Math::Abs(m_ScrollVelocity) > kVelocityActive)
    {
        // Velocity is in meters per 60 Hz frame; scale by the real step so a
        // flick travels the same distance at 30 fps as at 60 (the snap phase
        // below already did this; momentum did not).
        const float stepCoef = delta_time * 60.0f;
        m_ScrollOffset = ((m_ScrollOffset) + (m_ScrollVelocity * stepCoef));

        // Decelerate (friction), frame-rate independent: deceleration^(dt*60)
        m_ScrollVelocity = ((m_ScrollVelocity) * std::pow(deceleration, stepCoef));

        ClampScrollOffset();

        // When velocity gets low enough, start snapping to nearest item
        if (Deki::Math::Abs(m_ScrollVelocity) < kVelocityCutoff)
        {
            m_ScrollVelocity = 0.0f;
            SnapToNearestItem();
        }

        needsSync = true;
    }
    // Phase 2: Snap animation (smooth ease to target)
    else if (m_IsSnapping)
    {
        float diff = ((m_TargetOffset) - (m_ScrollOffset));

        // Simple ease-out: move a fraction of the remaining distance.
        // snapSpeed controls how fast (higher = faster snap). easeFactor is
        // a pure float coefficient (frame scaling), wrapped to mix safely.
        float easeFactorF = (snapSpeed) * 0.15f * delta_time * 60.0f;
        if (easeFactorF > 0.5f) easeFactorF = 0.5f;
        float easeFactor = static_cast<float>(easeFactorF);

        m_ScrollOffset = ((m_ScrollOffset) + (((diff) * (easeFactor))));

        // Check if we've arrived
        if (Deki::Math::Abs(((m_TargetOffset) - (m_ScrollOffset))) < kSnapArrived)
        {
            m_ScrollOffset = m_TargetOffset;
            m_IsSnapping = false;

            if (m_OnValueCommitted)
            {
                m_OnValueCommitted(selectedIndex, GetSelectedValue());
            }
        }

        ClampScrollOffset();
        needsSync = true;
    }

    UpdateSelection();

    // Update visual positions of text rows when animating
    if (needsSync && GetOwner())
        SyncChildObjects(GetOwner());
}

// ============================================================================
// LIFECYCLE METHODS FOR PLAY MODE
// ============================================================================

void RollerComponent::Start()
{
    // Initialize child object references when play mode starts
    // This is critical for duplicated rollers whose runtime pointers are null
    if (GetOwner())
        SyncChildObjects(GetOwner());

    InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        DEKI_LOG_WARNING("RollerComponent: No InputCollider referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

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

bool RollerComponent::NeedsRuntimeUpdate() const
{
    return true;  // Roller always needs update for physics/animation
}

void RollerComponent::RuntimeUpdate(float deltaTime)
{
    Update(deltaTime);
}

void RollerComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;

    // Sync child objects when any property changes
    // This updates TextComponent width/height and other derived values
    if (GetOwner())
    {
        SyncChildObjects(GetOwner());
    }
}
