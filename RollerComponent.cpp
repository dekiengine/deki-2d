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
#include <deki/assets/Texture2D.h>
#include "Sprite.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace Deki2D
{

// All sizes and positions in this file are world meters, like transform x/y
// and the pointer callbacks.

// ============================================================================
// Component Registration
// ============================================================================
// s_Properties[] and s_ComponentMeta are generated into RollerComponent.gen.h.

RollerComponent::RollerComponent()
    : Deki::Component(),
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
    // The owner Object owns the children; only drop the references.
    m_ClipObj = nullptr;
    m_BackgroundObj = nullptr;
    m_SelectionObj = nullptr;
    m_TextRowObjs.clear();
}

Deki::Object* RollerComponent::FindOrCreateChild(Deki::Object* owner, const char* name, const char* componentType)
{
    if (!owner)
    {
        return nullptr;
    }

    for (auto* child : owner->GetChildren())
    {
        if (child->GetName() == name)
        {
            return child;
        }
    }

#ifdef DEKI_EDITOR
    // Only the editor creates children; at runtime they come from the scene.
    Deki::Object* child = new Deki::Object(name);
    owner->AddChild(child);
    DekiEditor::AddComponentByName(child, componentType);
    return child;
#else
    // The scene should already hold the child.
    return nullptr;
#endif
}

void RollerComponent::UpdateTextRowCount(Deki::Object* owner)
{
    if (!owner)
    {
        return;
    }

    m_TextRowObjs.clear();
    for (auto* child : owner->GetChildren())
    {
        if (child->GetName().rfind("TextRow", 0) == 0)
        {
            child->SetActive(true);  // Re-enable in case it was hidden
            m_TextRowObjs.push_back(child);
        }
    }

    // Sort by the numeric suffix, not by name: a name sort puts TextRow10
    // before TextRow2.
    auto rowNumber = [](const Deki::Object* o) -> long
    {
        const std::string& n = o->GetName();
        return std::strtol(n.c_str() + 7, nullptr, 10);  // after "TextRow"
    };
    std::sort(m_TextRowObjs.begin(), m_TextRowObjs.end(),
              [&rowNumber](Deki::Object* a, Deki::Object* b) { return rowNumber(a) < rowNumber(b); });

    // One extra row above and one below, so rows scroll in instead of popping in.
    int32_t totalRows = visibleRows + 2;

    // New rows copy their font settings from an existing row that has a font.
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
            if (auto* tc = row->GetComponent<TextComponent>())
            {
                tc->align = TextAlign::Center;
                tc->verticalAlign = TextVerticalAlign::Middle;

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

    // Hide extra rows rather than delete them: the user may have customized them.
    for (size_t i = totalRows; i < m_TextRowObjs.size(); ++i)
    {
        m_TextRowObjs[i]->SetActive(false);
    }
    if (m_TextRowObjs.size() > static_cast<size_t>(totalRows))
    {
        m_TextRowObjs.resize(totalRows);
    }
}

void RollerComponent::EnsureChildObjects(Deki::Object* owner)
{
    if (!owner)
    {
        return;
    }

    m_ClipObj = FindOrCreateChild(owner, "Clip", "ClipComponent");

    // All other children go inside Clip, so they get clipped.
    if (m_ClipObj)
    {
        m_BackgroundObj = FindOrCreateChild(m_ClipObj, "Background", "SpriteComponent");

        m_SelectionObj = FindOrCreateChild(m_ClipObj, "Selection", "SpriteComponent");

        UpdateTextRowCount(m_ClipObj);
    }
}

bool RollerComponent::NeedsChildDiscovery(const Deki::Object* owner) const
{
    if (!owner)
    {
        return true;
    }
    if (!m_ClipObj)
    {
        return true;
    }
    if (owner->GetChildren().size() != m_DiscoveredOwnerChildren)
    {
        return true;
    }
    if (m_ClipObj->GetChildren().size() != m_DiscoveredClipChildren)
    {
        return true;
    }
    if (m_TextRowObjs.size() != static_cast<size_t>(visibleRows + 2))
    {
        return true;
    }
    return false;
}

void RollerComponent::SyncChildObjects(Deki::Object* owner)
{
    // Discovery (three name scans, a sort and a text copy per row) is too
    // slow for every pointer move and animation frame, so it runs only when
    // the children changed.
    if (NeedsChildDiscovery(owner))
    {
        EnsureChildObjects(owner);
        m_DiscoveredOwnerChildren = owner ? owner->GetChildren().size() : static_cast<size_t>(-1);
        m_DiscoveredClipChildren = m_ClipObj ? m_ClipObj->GetChildren().size() : static_cast<size_t>(-1);
    }

    // When selectedIndex or the heights change from outside (the inspector,
    // say), move the scroll offset to match, unless the roller is scrolling.
    bool indexChanged = (selectedIndex != m_LastSyncedSelectedIndex);
    bool heightsChanged =
        (selectedItemHeight != m_LastSyncedSelectedItemHeight || itemHeight != m_LastSyncedItemHeight);

    if (!m_IsDragging && !m_IsSnapping && (indexChanged || heightsChanged))
    {
        m_ScrollOffset = GetItemOffset(selectedIndex);
        m_LastSyncedSelectedIndex = selectedIndex;
        m_LastSyncedSelectedItemHeight = selectedItemHeight;
        m_LastSyncedItemHeight = itemHeight;
    }

    int32_t centerRow = visibleRows / 2;

    // The child positions are fixed; the user sizes the clip on its
    // ClipComponent and the sprites by texture or scale.
    if (m_ClipObj)
    {
        m_ClipObj->SetLocalPosition(0.0f, 0.0f);
    }

    if (m_BackgroundObj)
    {
        m_BackgroundObj->SetLocalPosition(0.0f, 0.0f);
    }

    // The selection sits on the centre row, which is Y=0.
    if (m_SelectionObj)
    {
        m_SelectionObj->SetLocalPosition(0.0f, 0.0f);
    }

    // Text rows. The item shown in the centre comes from the scroll offset.
    float scrollOffset = m_ScrollOffset;
    int32_t rawCenteredIndex = GetRawIndexAtOffset(scrollOffset);

    // The unwrapped index keeps subItemOffset small.
    float snapOffset = GetItemOffset(rawCenteredIndex);
    float subItemOffset = ((scrollOffset) - (snapOffset));

    // visibleRows + 2 rows: row 0 is one above the top visible row, the last
    // is one below the bottom.
    int32_t totalRows = visibleRows + 2;

    for (int32_t row = 0; row < static_cast<int32_t>(m_TextRowObjs.size()) && row < totalRows; row++)
    {
        Deki::Object* textObj = m_TextRowObjs[row];
        if (!textObj)
        {
            continue;
        }

        // Row 0 is position -1 (above the top), row 1 is position 0 (top visible), and so on.
        int32_t virtualRow = row - 1;

        // Built from the unwrapped centre index so offsets stay in the same range.
        int32_t itemIndex = rawCenteredIndex + (virtualRow - centerRow);
        bool validIndex = true;

        if (infiniteScroll)
        {
            int32_t optSize = static_cast<int32_t>(options.size());
            if (optSize > 0)
            {
                itemIndex = ((itemIndex % optSize) + optSize) % optSize;
            }
        }
        else
        {
            validIndex = (itemIndex >= 0 && itemIndex < static_cast<int32_t>(options.size()));
        }

        // The centre row always gets the selected style, so the layout stays steady.
        bool isCenterRow = (virtualRow == centerRow);
        float rowHeight = isCenterRow ? selectedItemHeight : itemHeight;

        if (auto* tc = textObj->GetComponent<TextComponent>())
        {
            tc->width = width;
            tc->height = rowHeight;

            tc->color = isCenterRow ? selectedColor : normalColor;

#ifdef DEKI_EDITOR
            // Drawing at the row's own size keeps the text sharp.
            tc->fontSize = isCenterRow ? selectedFontSize : normalFontSize;
#endif

            if (!options.empty())
            {
                if (infiniteScroll || validIndex)
                {
                    tc->text = options[itemIndex];
                }
                else
                {
                    // Past either end of a non-wrapping list.
                    tc->text.clear();
                }
            }
        }

        // Rows are placed outward from the edges of the centre row. The
        // centre row always stays at Y=0; only the others move with scroll.
        float rowY = 0.0f;
        int32_t rowDist = centerRow - virtualRow;  // positive = above center, negative = below

        if (rowDist > 0)
        {
            // Above centre: the first row's bottom touches the centre's top.
            float bottomEdge = ((selectedItemHeight) * (0.5f));
            bottomEdge = ((bottomEdge) + (((static_cast<float>(rowDist - 1)) * (itemHeight))));
            rowY = ((((bottomEdge) + (((rowHeight) * (0.5f))))) + (subItemOffset));
        }
        else if (rowDist < 0)
        {
            // Below centre: the first row's top touches the centre's bottom.
            float topEdge = ((0.0f) - (((selectedItemHeight) * (0.5f))));
            topEdge = ((topEdge) - (((static_cast<float>(-rowDist - 1)) * (itemHeight))));
            rowY = ((((topEdge) - (((rowHeight) * (0.5f))))) + (subItemOffset));
        }
        // The centre row (rowDist == 0) keeps rowY 0, without subItemOffset.

        textObj->SetLocalPosition(0.0f, rowY);
        // Active state is left to the user.
    }
}

SpriteComponent* RollerComponent::GetBackgroundSprite()
{
    if (m_BackgroundObj)
    {
        return m_BackgroundObj->GetComponent<SpriteComponent>();
    }
    return nullptr;
}

SpriteComponent* RollerComponent::GetSelectionSprite()
{
    if (m_SelectionObj)
    {
        return m_SelectionObj->GetComponent<SpriteComponent>();
    }
    return nullptr;
}

TextComponent* RollerComponent::GetTextComponent(int32_t row)
{
    if (row >= 0 && row < static_cast<int32_t>(m_TextRowObjs.size()))
    {
        if (m_TextRowObjs[row])
        {
            return m_TextRowObjs[row]->GetComponent<TextComponent>();
        }
    }
    return nullptr;
}

void RollerComponent::SetOptions(const std::vector<std::string>& newOptions)
{
    options = newOptions;

    if (selectedIndex >= static_cast<int32_t>(options.size()))
    {
        selectedIndex = options.empty() ? 0 : static_cast<int32_t>(options.size()) - 1;
    }

    m_ScrollOffset = GetItemOffset(selectedIndex);
    m_ScrollVelocity = 0.0f;
    m_IsSnapping = false;
    m_NeedsSync = true;
    if (GetOwner())
    {
        SyncChildObjects(GetOwner());
    }
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
    {
        return;
    }

    // Wrap with infinite scroll, clamp without.
    if (infiniteScroll)
    {
        index = ((index % static_cast<int32_t>(options.size())) + static_cast<int32_t>(options.size())) %
                static_cast<int32_t>(options.size());
    }
    else
    {
        index = std::max(static_cast<int32_t>(0), std::min(index, static_cast<int32_t>(options.size()) - 1));
    }

    int32_t oldIndex = selectedIndex;
    selectedIndex = index;
    m_LastSyncedSelectedIndex = index;  // so SyncChildObjects does not see an outside change

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
    // Always odd, so the selection is centred.
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
    // Offset that centres item `index`. The centre row is selectedItemHeight
    // tall, the others itemHeight, so the first step from the centre is half
    // of each height and every further step is itemHeight.
    int32_t centerRow = visibleRows / 2;
    int32_t dist = index - centerRow;

    if (dist == 0)
    {
        return 0.0f;
    }

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
    {
        return 0;
    }

    // An item is selected once more than half of it is in the centre row.
    // That matches the snap, so the highlighted item is the one it will snap to.

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
        index = ((index % static_cast<int32_t>(options.size())) + static_cast<int32_t>(options.size())) %
                static_cast<int32_t>(options.size());
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
    {
        return 0;
    }

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

    return index;
}

void RollerComponent::ClampScrollOffset()
{
    if (options.empty() || infiniteScroll)
    {
        return;
    }

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
    {
        return;
    }

    int32_t rawIndex = GetRawIndexAtOffset(m_ScrollOffset);

    // The unwrapped index keeps the target in the same range as m_ScrollOffset.
    m_TargetOffset = GetItemOffset(rawIndex);
    m_IsSnapping = true;
}

void RollerComponent::UpdateSelection()
{
    if (options.empty())
    {
        return;
    }

    int32_t rawIndex = GetRawIndexAtOffset(m_ScrollOffset);

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
        m_LastSyncedSelectedIndex = newIndex;  // so SyncChildObjects does not see an outside change

        if (m_OnSelectionChanged)
        {
            m_OnSelectionChanged(selectedIndex, GetSelectedValue());
        }
    }
}

void RollerComponent::HandlePointerDown(float x, float y)
{
    (void)x;
    // Pointer y is in world meters, like the scroll state.
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
    if (!m_IsDragging)
    {
        return;
    }

    float touchY = static_cast<float>(y);
    float delta = ((touchY) - (m_LastTouchY));

    // The content follows the finger, as in ScrollComponent; reverseDrag inverts it.
    float scrollDelta = reverseDrag ? ((0.0f) - (delta)) : delta;
    m_ScrollOffset = ((m_ScrollOffset) + (scrollDelta));

    // Momentum velocity, damped to a third so flicks do not fly too far.
    static const float kThird = static_cast<float>(1.0f / 3.0f);
    m_ScrollVelocity = ((scrollDelta) * (kThird));

    m_LastTouchY = touchY;

    ClampScrollOffset();
    UpdateSelection();

    if (GetOwner())
    {
        SyncChildObjects(GetOwner());
    }
}

void RollerComponent::HandlePointerUp(float x, float y)
{
    (void)x;
    if (!m_IsDragging)
    {
        return;
    }

    m_IsDragging = false;

    float touchY = static_cast<float>(y);

    // A move shorter than a third of an item is a tap, not a drag.
    static const float kThird = static_cast<float>(1.0f / 3.0f);
    float dragDistance = Deki::Math::Abs(((touchY) - (m_TouchStartY)));
    float tapThreshold = ((itemHeight) * (kThird));
    if (dragDistance < tapThreshold)
    {
        Deki::Object* owner = GetOwner();
        if (owner)
        {
            // Which row was tapped, all in meters.
            float centerY = owner->GetWorldY();
            float rollerTop = ((centerY) - (((GetHeight()) * (0.5f))));
            float relativeY = ((touchY) - (rollerTop));
            int32_t tappedRow = static_cast<int32_t>(std::floor(((relativeY) / (itemHeight))));

            if (tappedRow >= 0 && tappedRow < visibleRows)
            {
                int32_t centerRow = visibleRows / 2;
                int32_t rowOffset = tappedRow - centerRow;

                if (rowOffset != 0)
                {
                    int32_t targetIndex = selectedIndex + rowOffset;

                    // Wrap with infinite scroll, clamp without.
                    if (infiniteScroll)
                    {
                        int32_t optSize = static_cast<int32_t>(options.size());
                        targetIndex = ((targetIndex % optSize) + optSize) % optSize;
                    }
                    else
                    {
                        targetIndex = std::max(static_cast<int32_t>(0),
                                               std::min(targetIndex, static_cast<int32_t>(options.size()) - 1));
                    }

                    if (targetIndex != selectedIndex)
                    {
                        SetSelectedIndex(targetIndex, true);
                        return;
                    }
                }
            }
        }

        // A tap on the centre row or outside the rows just snaps.
        m_ScrollVelocity = 0.0f;
        SnapToNearestItem();
        return;
    }

    // A slow release (a slow drag, or stopped between items) snaps at once.
    // The threshold is about 3 px/frame at 16 pixels per meter.
    static const float kLowVelocity = static_cast<float>(0.1875f);
    if (Deki::Math::Abs(m_ScrollVelocity) < kLowVelocity)
    {
        m_ScrollVelocity = 0.0f;
        SnapToNearestItem();
    }
    // Otherwise momentum carries on and Update() snaps once it dies down.
}

void RollerComponent::Update(float deltaTime)
{
    if (m_IsDragging)
    {
        return;
    }

    bool needsSync = false;

    // Velocity thresholds in meters per frame, tuned in pixels at 16 pixels per meter.
    static const float kVelocityActive = static_cast<float>(0.0625f);  // > 1 px/frame
    static const float kVelocityCutoff = static_cast<float>(0.125f);   // < 2 px/frame triggers snap
    static const float kSnapArrived = static_cast<float>(0.004f);      // < ~0.06 px from target

    // Phase 1: momentum, after release and before the snap.
    if (!m_IsSnapping && Deki::Math::Abs(m_ScrollVelocity) > kVelocityActive)
    {
        // Velocity is in meters per 60 Hz frame. Scaling by the real step
        // makes a flick travel the same distance at 30 fps as at 60.
        const float stepCoef = deltaTime * 60.0f;
        m_ScrollOffset = ((m_ScrollOffset) + (m_ScrollVelocity * stepCoef));

        // Friction, independent of frame rate: deceleration^(dt*60).
        m_ScrollVelocity = ((m_ScrollVelocity)*std::pow(deceleration, stepCoef));

        ClampScrollOffset();

        if (Deki::Math::Abs(m_ScrollVelocity) < kVelocityCutoff)
        {
            m_ScrollVelocity = 0.0f;
            SnapToNearestItem();
        }

        needsSync = true;
    }
    // Phase 2: the snap, easing onto the target.
    else if (m_IsSnapping)
    {
        float diff = ((m_TargetOffset) - (m_ScrollOffset));

        // Ease out: each frame moves a fraction of the remaining distance.
        // Higher snapSpeed snaps faster. The fraction is capped at a half.
        float easeFactorF = (snapSpeed) * 0.15f * deltaTime * 60.0f;
        if (easeFactorF > 0.5f)
        {
            easeFactorF = 0.5f;
        }
        float easeFactor = static_cast<float>(easeFactorF);

        m_ScrollOffset = ((m_ScrollOffset) + (((diff) * (easeFactor))));

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

    if (needsSync && GetOwner())
    {
        SyncChildObjects(GetOwner());
    }
}

// ============================================================================
// LIFECYCLE METHODS FOR PLAY MODE
// ============================================================================

void RollerComponent::Start()
{
    // Find the children now: a duplicated roller starts with null child pointers.
    if (GetOwner())
    {
        SyncChildObjects(GetOwner());
    }

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        DEKI_LOG_WARNING("RollerComponent: No DekiInput::InputCollider referenced on '%s'",
                         GetOwner()->GetName().c_str());
        return;
    }

    collider->onPointerDown.push_back([this](float x, float y) { HandlePointerDown(x, y); });

    collider->onPointerMove.push_back([this](float x, float y) { HandlePointerMove(x, y); });

    collider->onPointerUp.push_back([this](float x, float y) { HandlePointerUp(x, y); });
}

bool RollerComponent::NeedsRuntimeUpdate() const
{
    return true;  // momentum and the snap run every frame
}

void RollerComponent::RuntimeUpdate(float deltaTime)
{
    Update(deltaTime);
}

void RollerComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;

    // Any change can affect the rows' size, colour or text.
    if (GetOwner())
    {
        SyncChildObjects(GetOwner());
    }
}

}  // namespace Deki2D
