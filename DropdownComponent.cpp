#include "DropdownComponent.h"

#include <deki/Object.h>
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "deki-rendering/RendererComponent.h"
#include "WidgetShapes.h"
#include "WidgetText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace Deki2D
{

namespace
{

// The open list draws above the widgets around it, and outside a clip.
constexpr int32_t kListSortingOrder = 1000;

// The catch-all behind the open list: a tap anywhere it does not cover
// closes it. Wider than any screen.
constexpr float kBlockerSize = 4000.0f;

void RaisePart(Deki::Object* part)
{
    if (!part)
    {
        return;
    }
    if (auto* renderer = part->GetComponent<DekiRendering::RendererComponent>())
    {
        renderer->sortingOrder = kListSortingOrder;
        renderer->ignoreClip = true;
    }
}

const std::string kNoOption;

}  // namespace

DropdownComponent::~DropdownComponent()
{
    if (m_Open && m_List)
    {
        FocusSystem::PopScope(m_List);
    }
    FocusSystem::Unregister(this);
}

void DropdownComponent::Start()
{
    SyncParts();
    FocusSystem::Register(this);
    if (m_List)
    {
        m_List->SetActive(false);
    }

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        return;
    }
    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            DekiInput::InputCollider* col = inputCollider.Get();
            if (isEnabled && !m_Open && col && col->IsPointerInside() && !FocusSystem::IsInputBlocked(GetOwner()))
            {
                Open();
            }
        });
    ListenToList();
}

void DropdownComponent::ListenToList()
{
    if (m_Listening || !m_List || !m_Rows)
    {
        return;
    }
    auto* blocker = m_List->GetComponent<DekiInput::InputCollider>();
    auto* rows = m_Rows->GetComponent<DekiInput::InputCollider>();
    if (!blocker || !rows)
    {
        return;
    }
    m_Listening = true;
    blocker->onPointerDown.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            Close();
        });
    rows->onPointerDown.push_back(
        [this](float x, float y)
        {
            (void)x;
            m_PressedRow = OptionAt(y);
            if (m_PressedRow >= 0)
            {
                MoveHighlight(m_PressedRow);
            }
        });
    rows->onPointerMove.push_back(
        [this](float x, float y)
        {
            (void)x;
            const int32_t option = OptionAt(y);
            if (option >= 0)
            {
                MoveHighlight(option);
            }
        });
    rows->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            const int32_t option = OptionAt(y);
            if (option >= 0 && option == m_PressedRow)
            {
                SetSelectedIndex(option);
                Close();
            }
            m_PressedRow = -1;
        });
}

void DropdownComponent::SetSelectedIndex(int32_t index, bool notify)
{
    if (options.empty())
    {
        return;
    }
    index = std::clamp(index, 0, static_cast<int32_t>(options.size()) - 1);
    if (index == selectedIndex)
    {
        return;
    }
    selectedIndex = index;
    SyncParts();
    if (notify)
    {
        for (const auto& callback : onSelectionChanged)
        {
            callback(selectedIndex, options[selectedIndex]);
        }
    }
}

const std::string& DropdownComponent::GetSelectedValue() const
{
    if (selectedIndex >= 0 && selectedIndex < static_cast<int32_t>(options.size()))
    {
        return options[selectedIndex];
    }
    return kNoOption;
}

void DropdownComponent::AddOnSelectionChangedCallback(const std::function<void(int32_t, const std::string&)>& callback)
{
    onSelectionChanged.push_back(callback);
}

void DropdownComponent::Open()
{
    if (m_Open || options.empty() || !m_List)
    {
        return;
    }
    m_Open = true;
    m_Highlight = std::clamp(selectedIndex, 0, static_cast<int32_t>(options.size()) - 1);
    // The chosen option in view, as near the top as the list allows.
    const int32_t rows = std::min<int32_t>(static_cast<int32_t>(m_RowParts.size()), listRows);
    m_Scroll = std::clamp(m_Highlight, 0, std::max(0, static_cast<int32_t>(options.size()) - rows));
    m_List->SetActive(true);
    // A scope round the list: what is under it ignores taps until it
    // closes, and its keys come here whether or not it has the focus.
    m_RestoreFocus = FocusSystem::GetFocused() == this;
    FocusSystem::PushScope(m_List, this);
    m_LastLayout = 0;
    SyncParts();
}

void DropdownComponent::Close()
{
    if (!m_Open)
    {
        return;
    }
    m_Open = false;
    m_PressedRow = -1;
    if (m_List)
    {
        FocusSystem::PopScope(m_List);
        m_List->SetActive(false);
    }
    if (m_RestoreFocus)
    {
        FocusSystem::SetFocus(this);
        m_RestoreFocus = false;
    }
    m_LastLayout = 0;
    SyncParts();
}

int32_t DropdownComponent::OptionAt(float worldY) const
{
    if (!m_List || height <= 0.0f)
    {
        return -1;
    }
    const float top = m_List->GetWorldY() + ListHeight() * 0.5f;
    const int32_t row = static_cast<int32_t>(std::floor((top - worldY) / height));
    const int32_t rows = std::min<int32_t>(static_cast<int32_t>(m_RowParts.size()), listRows);
    if (row < 0 || row >= rows)
    {
        return -1;
    }
    const int32_t option = m_Scroll + row;
    return option < static_cast<int32_t>(options.size()) ? option : -1;
}

void DropdownComponent::MoveHighlight(int32_t index)
{
    if (options.empty())
    {
        return;
    }
    m_Highlight = std::clamp(index, 0, static_cast<int32_t>(options.size()) - 1);
    const int32_t rows = std::max<int32_t>(1, std::min<int32_t>(static_cast<int32_t>(m_RowParts.size()), listRows));
    if (m_Highlight < m_Scroll)
    {
        m_Scroll = m_Highlight;
    }
    else if (m_Highlight >= m_Scroll + rows)
    {
        m_Scroll = m_Highlight - rows + 1;
    }
    SyncParts();
}

float DropdownComponent::ListHeight() const
{
    const int32_t rows = std::min<int32_t>(static_cast<int32_t>(options.size()), listRows);
    return height * static_cast<float>(std::max(1, rows));
}

void DropdownComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void DropdownComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void DropdownComponent::OnFocusChanged(bool focused)
{
    // The list stays open when the focus goes: a tap on one of its rows
    // takes the focus away first, and must still land on the row.
    m_Focused = focused;
    WidgetParts::ShowFocus(GetOwner(), focused);
}

bool DropdownComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    namespace Keys = DekiInput::Keys;
    const bool pick = key == Keys::Enter || key == Keys::Space;
    if (!m_Open)
    {
        if (pick)
        {
            Open();
            return true;
        }
        return false;
    }
    // Open, the list keeps every arrow: the focus stays until it closes.
    if (key == Keys::Up || key == Keys::Down)
    {
        MoveHighlight(m_Highlight + (key == Keys::Down ? 1 : -1));
        return true;
    }
    if (pick)
    {
        SetSelectedIndex(m_Highlight);
        Close();
        return true;
    }
    if (key == Keys::Esc)
    {
        Close();
        return true;
    }
    if (key == Keys::Tab)
    {
        Close();
        return false;  // and the focus moves on
    }
    return key == Keys::Left || key == Keys::Right;
}

void DropdownComponent::SyncParts()
{
    Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return;
    }
    const size_t listChildren = m_List ? m_List->GetChildren().size() : static_cast<size_t>(-1);
    if (owner->GetChildren().size() != m_KnownChildren || listChildren != m_KnownListChildren ||
        static_cast<int32_t>(m_RowParts.size()) < listRows)
    {
        EnsureParts();
        m_KnownChildren = owner->GetChildren().size();
        m_KnownListChildren = m_List ? m_List->GetChildren().size() : static_cast<size_t>(-1);
        m_LastLayout = 0;
    }
    DekiRendering::Fingerprint f;
    f.Add(width);
    f.Add(height);
    f.Add(selectedIndex);
    f.Add(listRows);
    f.Add(static_cast<uint32_t>(options.size()));
    f.Add(static_cast<uint32_t>(m_Open));
    f.Add(m_Highlight);
    f.Add(m_Scroll);
    // The texts by content: an option can be edited in place.
    for (const std::string& option : options)
    {
        uint32_t h = 2166136261u;
        for (char c : option)
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

void DropdownComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    const float corner = height * 0.25f;
    m_Field = WidgetParts::ShapePart(owner, "Field", ShapeKind::Rectangle, WidgetColors::Panel, corner);
    m_Label = WidgetParts::TextPart(owner, "Label", WidgetColors::Text, TextAlign::Left);
    m_Arrow = WidgetParts::ShapePart(owner, "Arrow", ShapeKind::Triangle, WidgetColors::DimText);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Rectangle, WidgetColors::Focus, corner * 1.5f);
    if (m_Focus)
    {
        m_Focus->SetActive(m_Focused);
    }

    bool created = false;
    m_List = WidgetParts::FindOrCreate<DekiInput::InputCollider>(owner, "List", created);
    if (created && m_List)
    {
        auto* blocker = m_List->GetComponent<DekiInput::InputCollider>();
        blocker->width = kBlockerSize;
        blocker->height = kBlockerSize;
        m_List->SetActive(m_Open);
    }
    if (m_List)
    {
        m_Back = WidgetParts::ShapePart(m_List, "Back", ShapeKind::Rectangle, WidgetColors::Panel, corner);
        m_HighlightPart =
            WidgetParts::ShapePart(m_List, "Highlight", ShapeKind::Rectangle, WidgetColors::Accent, corner);
        m_Rows = WidgetParts::FindOrCreate<DekiInput::InputCollider>(m_List, "Rows", created);
        RaisePart(m_Back);
        RaisePart(m_HighlightPart);
        m_RowParts.clear();
        for (int32_t i = 0; i < listRows; ++i)
        {
            char name[16];
            std::snprintf(name, sizeof(name), "Row%d", static_cast<int>(i));
            Deki::Object* row = WidgetParts::TextPart(m_List, name, WidgetColors::Text, TextAlign::Left);
            if (!row)
            {
                break;  // on the device: the rows the scene has
            }
            RaisePart(row);
            m_RowParts.push_back(row);
        }
    }
#ifdef DEKI_EDITOR
    if (owner && !inputCollider.GetObject())
    {
        if (!owner->GetComponent<DekiInput::InputCollider>())
        {
            owner->AddComponent<DekiInput::InputCollider>();
        }
        inputCollider.Set(owner);
    }
#endif
    if (HasStarted())
    {
        ListenToList();
    }
}

void DropdownComponent::LayoutParts()
{
    const float pad = height * 0.4f;
    const float arrow = height * 0.35f;
    WidgetParts::PlaceShape(m_Field, 0.0f, 0.0f, width, height);
    WidgetParts::PlaceShape(m_Arrow, width * 0.5f - pad - arrow * 0.5f, 0.0f, arrow, arrow * 0.6f);
    const float labelWidth = std::max(0.0f, width - pad * 3.0f - arrow);
    WidgetParts::PlaceText(m_Label, -width * 0.5f + pad + labelWidth * 0.5f, 0.0f, labelWidth, height);
    WidgetParts::SetPartText(m_Label, GetSelectedValue());
    if (ShapeComponent* shape = WidgetParts::Shape(m_Arrow))
    {
        shape->direction = m_Open ? ShapeDirection::Up : ShapeDirection::Down;
    }

    const float ring = height * 0.3f;
    WidgetParts::PlaceShape(m_Focus, 0.0f, 0.0f, width + ring, height + ring);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Focus))
    {
        shape->lineWidth = height * 0.08f;
    }

    // The list hangs below the field.
    const float listHeight = ListHeight();
    if (m_List)
    {
        m_List->SetLocalPosition(0.0f, -height * 0.5f - height * 0.1f - listHeight * 0.5f);
    }
    WidgetParts::PlaceShape(m_Back, 0.0f, 0.0f, width, listHeight);
    if (m_Rows)
    {
        m_Rows->SetLocalPosition(0.0f, 0.0f);
        if (auto* rows = m_Rows->GetComponent<DekiInput::InputCollider>())
        {
            rows->width = width;
            rows->height = listHeight;
        }
    }
    const int32_t shown = std::min<int32_t>(static_cast<int32_t>(m_RowParts.size()), listRows);
    for (int32_t i = 0; i < static_cast<int32_t>(m_RowParts.size()); ++i)
    {
        Deki::Object* row = m_RowParts[i];
        const int32_t option = m_Scroll + i;
        const bool used = i < shown && option < static_cast<int32_t>(options.size());
        row->SetActive(used);
        if (!used)
        {
            continue;
        }
        const float y = listHeight * 0.5f - height * (static_cast<float>(i) + 0.5f);
        WidgetParts::PlaceText(row, 0.0f, y, std::max(0.0f, width - pad * 2.0f), height);
        WidgetParts::SetPartText(row, options[option]);
    }
    const int32_t highlightRow = m_Highlight - m_Scroll;
    if (m_HighlightPart)
    {
        const bool visible = highlightRow >= 0 && highlightRow < shown;
        m_HighlightPart->SetActive(visible);
        const float y = listHeight * 0.5f - height * (static_cast<float>(highlightRow) + 0.5f);
        WidgetParts::PlaceShape(m_HighlightPart, 0.0f, y, width - height * 0.2f, height * 0.85f);
    }

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (collider && inputCollider.GetObject() == GetOwner())
    {
        collider->width = width;
        collider->height = height;
    }
}

}  // namespace Deki2D
