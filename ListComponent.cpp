#include "ListComponent.h"

#include <deki/Object.h>
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"
#include "WidgetText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Deki2D
{

namespace
{

// How a section title starts.
constexpr const char* kTitlePrefix = "# ";
constexpr size_t kTitlePrefixLength = 2;

}  // namespace

ListComponent::~ListComponent()
{
    FocusSystem::Unregister(this);
}

void ListComponent::Start()
{
    SyncParts();
    FocusSystem::Register(this);
    Listen();
}

void ListComponent::Listen()
{
    if (m_Listening || !m_Rows)
    {
        return;
    }
    auto* rows = m_Rows->GetComponent<DekiInput::InputCollider>();
    if (!rows)
    {
        return;
    }
    m_Listening = true;
    rows->onPointerDown.push_back(
        [this](float x, float y)
        {
            (void)x;
            if (!isEnabled || FocusSystem::IsInputBlocked(GetOwner()))
            {
                m_PressedItem = -1;
                return;
            }
            m_PressedItem = ItemAt(y);
            m_PressY = y;
            m_PressScroll = m_Scroll;
            m_Dragged = false;
            if (m_PressedItem >= 0 && !IsTitle(m_PressedItem))
            {
                m_Highlight = m_PressedItem;
                SyncParts();
            }
        });
    rows->onPointerMove.push_back(
        [this](float x, float y)
        {
            (void)x;
            if (m_PressedItem < 0 && !m_Dragged)
            {
                return;
            }
            // A drag of half a row starts scrolling, and the press is no
            // longer a tap. Dragging up shows the items further down.
            const float moved = y - m_PressY;
            if (!m_Dragged && std::fabs(moved) < rowHeight * 0.5f)
            {
                return;
            }
            m_Dragged = true;
            const int32_t rowsMoved = static_cast<int32_t>(std::lround(moved / rowHeight));
            ScrollTo(m_PressScroll + rowsMoved);
        });
    rows->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            if (!m_Dragged && m_PressedItem >= 0 && ItemAt(y) == m_PressedItem)
            {
                Click(m_PressedItem);
            }
            m_PressedItem = -1;
            m_Dragged = false;
        });
}

void ListComponent::AddOnItemClickedCallback(const std::function<void(int32_t, const std::string&)>& callback)
{
    onItemClicked.push_back(callback);
}

void ListComponent::Click(int32_t index)
{
    if (!isEnabled || index < 0 || index >= static_cast<int32_t>(items.size()) || IsTitle(index))
    {
        return;
    }
    m_Highlight = index;
    SyncParts();
    const std::string text = GetItemText(index);
    for (const auto& callback : onItemClicked)
    {
        callback(index, text);
    }
}

bool ListComponent::IsTitle(int32_t index) const
{
    return index >= 0 && index < static_cast<int32_t>(items.size()) &&
           items[index].compare(0, kTitlePrefixLength, kTitlePrefix) == 0;
}

std::string ListComponent::GetItemText(int32_t index) const
{
    if (index < 0 || index >= static_cast<int32_t>(items.size()))
    {
        return {};
    }
    return IsTitle(index) ? items[index].substr(kTitlePrefixLength) : items[index];
}

int32_t ListComponent::RowsShown() const
{
    return std::max<int32_t>(1, std::min<int32_t>(visibleRows, static_cast<int32_t>(m_RowParts.size())));
}

int32_t ListComponent::MaxScroll() const
{
    return std::max<int32_t>(0, static_cast<int32_t>(items.size()) - RowsShown());
}

void ListComponent::ScrollTo(int32_t index)
{
    const int32_t scroll = std::clamp<int32_t>(index, 0, MaxScroll());
    if (scroll != m_Scroll)
    {
        m_Scroll = scroll;
        SyncParts();
    }
}

float ListComponent::ListHeight() const
{
    return rowHeight * static_cast<float>(std::max<int32_t>(1, visibleRows));
}

int32_t ListComponent::ItemAt(float worldY) const
{
    const Deki::Object* owner = GetOwner();
    if (!owner || rowHeight <= 0.0f)
    {
        return -1;
    }
    const float top = owner->GetWorldY() + ListHeight() * 0.5f;
    const int32_t row = static_cast<int32_t>(std::floor((top - worldY) / rowHeight));
    if (row < 0 || row >= RowsShown())
    {
        return -1;
    }
    const int32_t item = m_Scroll + row;
    return item < static_cast<int32_t>(items.size()) ? item : -1;
}

int32_t ListComponent::NextClickable(int32_t from, int32_t step) const
{
    for (int32_t i = from; i >= 0 && i < static_cast<int32_t>(items.size()); i += step)
    {
        if (!IsTitle(i))
        {
            return i;
        }
    }
    return -1;
}

void ListComponent::MoveHighlight(int32_t index)
{
    if (index < 0)
    {
        return;
    }
    m_Highlight = index;
    // In view: a title just above it shows too, so the section is named.
    const int32_t rows = RowsShown();
    const int32_t top = (index > 0 && IsTitle(index - 1)) ? index - 1 : index;
    if (top < m_Scroll)
    {
        m_Scroll = top;
    }
    else if (index >= m_Scroll + rows)
    {
        m_Scroll = index - rows + 1;
    }
    m_Scroll = std::clamp<int32_t>(m_Scroll, 0, MaxScroll());
    SyncParts();
}

void ListComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void ListComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void ListComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    WidgetParts::ShowFocus(GetOwner(), focused);
    // Focus arriving shows where the arrows start.
    if (focused && m_Highlight < 0)
    {
        MoveHighlight(NextClickable(m_Scroll, 1));
    }
}

bool ListComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    namespace Keys = DekiInput::Keys;
    if (key == Keys::Up || key == Keys::Down)
    {
        const int32_t step = key == Keys::Down ? 1 : -1;
        const int32_t next = NextClickable(m_Highlight < 0 ? m_Scroll : m_Highlight + step, step);
        if (next < 0)
        {
            return false;  // past the end: the focus moves on
        }
        MoveHighlight(next);
        return true;
    }
    if ((key == Keys::Enter || key == Keys::Space) && m_Highlight >= 0)
    {
        Click(m_Highlight);
        return true;
    }
    return false;
}

void ListComponent::SyncParts()
{
    Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return;
    }
    if (owner->GetChildren().size() != m_KnownChildren || static_cast<int32_t>(m_RowParts.size()) < visibleRows)
    {
        EnsureParts();
        m_KnownChildren = owner->GetChildren().size();
        m_LastLayout = 0;
    }
    m_Scroll = std::clamp<int32_t>(m_Scroll, 0, MaxScroll());
    if (m_Highlight >= static_cast<int32_t>(items.size()))
    {
        m_Highlight = -1;
    }
    DekiRendering::Fingerprint f;
    f.Add(width);
    f.Add(rowHeight);
    f.Add(visibleRows);
    f.Add(static_cast<uint32_t>(items.size()));
    f.Add(static_cast<uint32_t>(isEnabled));
    f.Add(m_Highlight);
    f.Add(m_Scroll);
    // The texts by content: an item can be edited in place.
    for (const std::string& item : items)
    {
        uint32_t h = 2166136261u;
        for (char c : item)
        {
            h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
        }
        f.Add(h);
    }
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void ListComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    const float corner = rowHeight * 0.25f;
    m_Back = WidgetParts::ShapePart(owner, "Back", ShapeKind::Rectangle, WidgetColors::Panel, corner);
    m_HighlightPart = WidgetParts::ShapePart(owner, "Highlight", ShapeKind::Rectangle, WidgetColors::Track, corner);
    m_Thumb = WidgetParts::ShapePart(owner, "Thumb", ShapeKind::Rectangle, WidgetColors::DimText, corner);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Rectangle, WidgetColors::Focus, corner * 1.5f);
    if (m_Focus)
    {
        m_Focus->SetActive(m_Focused);
    }
    bool created = false;
    m_Rows = WidgetParts::FindOrCreate<DekiInput::InputCollider>(owner, "Rows", created);
    m_RowParts.clear();
    for (int32_t i = 0; i < visibleRows; ++i)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "Row%d", static_cast<int>(i));
        Deki::Object* row = WidgetParts::TextPart(owner, name, WidgetColors::Text, TextAlign::Left);
        if (!row)
        {
            break;  // on the device: the rows the scene has
        }
        m_RowParts.push_back(row);
    }
    if (HasStarted())
    {
        Listen();
    }
}

void ListComponent::LayoutParts()
{
    const float listHeight = ListHeight();
    const float pad = rowHeight * 0.4f;
    const int32_t rows = RowsShown();
    const bool scrolls = static_cast<int32_t>(items.size()) > rows;
    const float thumbWidth = rowHeight * 0.12f;
    const float textWidth = std::max(0.0f, width - pad * 2.0f - (scrolls ? thumbWidth + pad * 0.5f : 0.0f));

    WidgetParts::PlaceShape(m_Back, 0.0f, 0.0f, width, listHeight);
    if (m_Rows)
    {
        m_Rows->SetLocalPosition(0.0f, 0.0f);
        if (auto* collider = m_Rows->GetComponent<DekiInput::InputCollider>())
        {
            collider->width = width;
            collider->height = listHeight;
        }
    }

    for (int32_t i = 0; i < static_cast<int32_t>(m_RowParts.size()); ++i)
    {
        Deki::Object* row = m_RowParts[i];
        const int32_t item = m_Scroll + i;
        const bool used = i < rows && item < static_cast<int32_t>(items.size());
        row->SetActive(used);
        if (!used)
        {
            continue;
        }
        const float y = listHeight * 0.5f - rowHeight * (static_cast<float>(i) + 0.5f);
        WidgetParts::PlaceText(row, -width * 0.5f + pad + textWidth * 0.5f, y, textWidth, rowHeight);
        WidgetParts::SetPartText(row, GetItemText(item));
        if (TextComponent* text = WidgetParts::Text(row))
        {
            text->color = (IsTitle(item) || !isEnabled) ? WidgetColors::DimText : WidgetColors::Text;
        }
    }

    const int32_t highlightRow = m_Highlight - m_Scroll;
    if (m_HighlightPart)
    {
        const bool visible = m_Highlight >= 0 && highlightRow >= 0 && highlightRow < rows;
        m_HighlightPart->SetActive(visible);
        const float y = listHeight * 0.5f - rowHeight * (static_cast<float>(highlightRow) + 0.5f);
        WidgetParts::PlaceShape(m_HighlightPart, 0.0f, y, width - rowHeight * 0.2f, rowHeight * 0.85f);
    }

    // The thumb: as tall as the share of the items in view, as low as the
    // scroll has gone.
    if (m_Thumb)
    {
        m_Thumb->SetActive(scrolls);
        if (scrolls)
        {
            const float track = listHeight - pad;
            const float share = static_cast<float>(rows) / static_cast<float>(items.size());
            const float thumbHeight = std::max(rowHeight * 0.5f, track * share);
            const float travel = track - thumbHeight;
            const float along = MaxScroll() > 0 ? static_cast<float>(m_Scroll) / static_cast<float>(MaxScroll()) : 0.0f;
            const float y = track * 0.5f - thumbHeight * 0.5f - travel * along;
            WidgetParts::PlaceShape(m_Thumb, width * 0.5f - pad * 0.5f - thumbWidth * 0.5f, y, thumbWidth, thumbHeight);
        }
    }

    const float ring = rowHeight * 0.3f;
    WidgetParts::PlaceShape(m_Focus, 0.0f, 0.0f, width + ring, listHeight + ring);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Focus))
    {
        shape->lineWidth = rowHeight * 0.08f;
    }
}

}  // namespace Deki2D
