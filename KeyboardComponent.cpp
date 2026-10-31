#include "KeyboardComponent.h"

#include <deki/Engine.h>
#include <deki/Object.h>
#include "TextareaComponent.h"
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

// One key: its label, its width in tenths of the keyboard, what it does.
struct KeyDef
{
    const char* label;
    float units;
    KeyboardAction action;
};

struct KeyRow
{
    const KeyDef* keys;
    int32_t count;
    float indent;  // in key widths
};

constexpr KeyboardAction kType = KeyboardAction::Type;

constexpr KeyDef kLower0[] = { { "q", 1, kType }, { "w", 1, kType }, { "e", 1, kType }, { "r", 1, kType },
                               { "t", 1, kType }, { "y", 1, kType }, { "u", 1, kType }, { "i", 1, kType },
                               { "o", 1, kType }, { "p", 1, kType } };
constexpr KeyDef kLower1[] = { { "a", 1, kType }, { "s", 1, kType }, { "d", 1, kType },
                               { "f", 1, kType }, { "g", 1, kType }, { "h", 1, kType },
                               { "j", 1, kType }, { "k", 1, kType }, { "l", 1, kType } };
constexpr KeyDef kLower2[] = { { "Aa", 1.5f, KeyboardAction::Shift },
                               { "z", 1, kType },
                               { "x", 1, kType },
                               { "c", 1, kType },
                               { "v", 1, kType },
                               { "b", 1, kType },
                               { "n", 1, kType },
                               { "m", 1, kType },
                               { "<", 1.5f, KeyboardAction::Backspace } };
constexpr KeyDef kUpper0[] = { { "Q", 1, kType }, { "W", 1, kType }, { "E", 1, kType }, { "R", 1, kType },
                               { "T", 1, kType }, { "Y", 1, kType }, { "U", 1, kType }, { "I", 1, kType },
                               { "O", 1, kType }, { "P", 1, kType } };
constexpr KeyDef kUpper1[] = { { "A", 1, kType }, { "S", 1, kType }, { "D", 1, kType },
                               { "F", 1, kType }, { "G", 1, kType }, { "H", 1, kType },
                               { "J", 1, kType }, { "K", 1, kType }, { "L", 1, kType } };
constexpr KeyDef kUpper2[] = { { "Aa", 1.5f, KeyboardAction::Shift },
                               { "Z", 1, kType },
                               { "X", 1, kType },
                               { "C", 1, kType },
                               { "V", 1, kType },
                               { "B", 1, kType },
                               { "N", 1, kType },
                               { "M", 1, kType },
                               { "<", 1.5f, KeyboardAction::Backspace } };
constexpr KeyDef kLetters3[] = { { "123", 2, KeyboardAction::Symbols },
                                 { "", 6, KeyboardAction::Space },
                                 { "OK", 2, KeyboardAction::Enter } };
constexpr KeyDef kSymbols0[] = { { "1", 1, kType }, { "2", 1, kType }, { "3", 1, kType }, { "4", 1, kType },
                                 { "5", 1, kType }, { "6", 1, kType }, { "7", 1, kType }, { "8", 1, kType },
                                 { "9", 1, kType }, { "0", 1, kType } };
constexpr KeyDef kSymbols1[] = { { "-", 1, kType }, { "/", 1, kType }, { ":", 1, kType },
                                 { ";", 1, kType }, { "(", 1, kType }, { ")", 1, kType },
                                 { "$", 1, kType }, { "&", 1, kType }, { "@", 1, kType } };
constexpr KeyDef kSymbols2[] = { { "#", 1, kType },  { ".", 1, kType }, { ",", 1, kType },
                                 { "?", 1, kType },  { "!", 1, kType }, { "'", 1, kType },
                                 { "\"", 1, kType }, { "+", 1, kType }, { "<", 2, KeyboardAction::Backspace } };
constexpr KeyDef kSymbols3[] = { { "abc", 2, KeyboardAction::Letters },
                                 { "", 6, KeyboardAction::Space },
                                 { "OK", 2, KeyboardAction::Enter } };

constexpr int32_t kKeyRows = 4;
constexpr float kRowUnits = 10.0f;

const KeyRow kLowerRows[kKeyRows] = {
    { kLower0, 10, 0 }, { kLower1, 9, 0.5f }, { kLower2, 9, 0 }, { kLetters3, 3, 0 }
};
const KeyRow kUpperRows[kKeyRows] = {
    { kUpper0, 10, 0 }, { kUpper1, 9, 0.5f }, { kUpper2, 9, 0 }, { kLetters3, 3, 0 }
};
const KeyRow kSymbolRows[kKeyRows] = {
    { kSymbols0, 10, 0 }, { kSymbols1, 9, 0.5f }, { kSymbols2, 9, 0 }, { kSymbols3, 3, 0 }
};

// The most keys any layout has: one label each.
constexpr int32_t kMaxKeys = 31;

const KeyRow* RowsFor(KeyboardMode mode)
{
    switch (mode)
    {
        case KeyboardMode::Upper: return kUpperRows;
        case KeyboardMode::Symbols: return kSymbolRows;
        case KeyboardMode::Lower: break;
    }
    return kLowerRows;
}

// Key `index`'s row and its definition.
const KeyDef* FindKey(KeyboardMode mode, int32_t index, int32_t* outRow = nullptr, int32_t* outColumn = nullptr)
{
    const KeyRow* rows = RowsFor(mode);
    for (int32_t r = 0; r < kKeyRows; ++r)
    {
        if (index < rows[r].count)
        {
            if (outRow)
            {
                *outRow = r;
            }
            if (outColumn)
            {
                *outColumn = index;
            }
            return index >= 0 ? &rows[r].keys[index] : nullptr;
        }
        index -= rows[r].count;
    }
    return nullptr;
}

// Keys like shift and delete wear the darker cap.
bool IsSpecial(KeyboardAction action)
{
    return action != KeyboardAction::Type && action != KeyboardAction::Space;
}

}  // namespace

KeyboardComponent::~KeyboardComponent()
{
    FocusSystem::Unregister(this);
}

int32_t KeyboardComponent::GetKeyCount() const
{
    const KeyRow* rows = RowsFor(mode);
    int32_t count = 0;
    for (int32_t r = 0; r < kKeyRows; ++r)
    {
        count += rows[r].count;
    }
    return count;
}

const char* KeyboardComponent::GetKeyLabel(int32_t index) const
{
    const KeyDef* key = FindKey(mode, index);
    return key ? key->label : "";
}

void KeyboardComponent::KeyBox(int32_t index, float& x, float& y, float& w, float& h) const
{
    int32_t row = 0;
    int32_t column = 0;
    x = y = w = h = 0.0f;
    if (!FindKey(mode, index, &row, &column))
    {
        return;
    }
    const KeyRow& keys = RowsFor(mode)[row];
    const float unit = width / kRowUnits;
    h = height / static_cast<float>(kKeyRows);
    y = height * 0.5f - h * (static_cast<float>(row) + 0.5f);
    float left = -width * 0.5f + keys.indent * unit;
    for (int32_t c = 0; c < column; ++c)
    {
        left += keys.keys[c].units * unit;
    }
    w = keys.keys[column].units * unit;
    x = left + w * 0.5f;
}

int32_t KeyboardComponent::KeyAt(float worldX, float worldY) const
{
    const Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return -1;
    }
    const float lx = worldX - owner->GetWorldX();
    const float ly = worldY - owner->GetWorldY();
    const int32_t count = GetKeyCount();
    for (int32_t i = 0; i < count; ++i)
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        KeyBox(i, x, y, w, h);
        if (std::fabs(lx - x) <= w * 0.5f && std::fabs(ly - y) <= h * 0.5f)
        {
            return i;
        }
    }
    return -1;
}

int32_t KeyboardComponent::KeyInRow(int32_t from, int32_t rowStep) const
{
    int32_t row = 0;
    if (!FindKey(mode, from, &row))
    {
        return -1;
    }
    const int32_t targetRow = row + rowStep;
    if (targetRow < 0 || targetRow >= kKeyRows)
    {
        return -1;
    }
    float fx = 0.0f;
    float fy = 0.0f;
    float fw = 0.0f;
    float fh = 0.0f;
    KeyBox(from, fx, fy, fw, fh);
    int32_t best = -1;
    float bestGap = 0.0f;
    const int32_t count = GetKeyCount();
    for (int32_t i = 0; i < count; ++i)
    {
        int32_t r = 0;
        FindKey(mode, i, &r);
        if (r != targetRow)
        {
            continue;
        }
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        KeyBox(i, x, y, w, h);
        const float gap = std::fabs(x - fx);
        if (best < 0 || gap < bestGap)
        {
            best = i;
            bestGap = gap;
        }
    }
    return best;
}

void KeyboardComponent::AddOnKeyCallback(const std::function<void(KeyboardAction, const char*)>& callback)
{
    onKey.push_back(callback);
}

void KeyboardComponent::PressKey(int32_t index)
{
    const KeyDef* key = FindKey(mode, index);
    if (!key || !isEnabled)
    {
        return;
    }
    TextareaComponent* box = target.Get();
    switch (key->action)
    {
        case KeyboardAction::Type:
            if (box)
            {
                box->InsertText(key->label);
            }
            break;
        case KeyboardAction::Space:
            if (box)
            {
                box->InsertText(" ");
            }
            break;
        case KeyboardAction::Backspace:
            if (box)
            {
                box->Backspace();
            }
            break;
        case KeyboardAction::Enter:
            if (box)
            {
                box->Enter();
            }
            break;
        case KeyboardAction::Shift:
            mode = mode == KeyboardMode::Upper ? KeyboardMode::Lower : KeyboardMode::Upper;
            break;
        case KeyboardAction::Symbols: mode = KeyboardMode::Symbols; break;
        case KeyboardAction::Letters: mode = KeyboardMode::Lower; break;
    }
    const KeyboardAction action = key->action;
    const char* typed = action == KeyboardAction::Type ? key->label : (action == KeyboardAction::Space ? " " : "");
    for (const auto& callback : onKey)
    {
        callback(action, typed);
    }
    // A new layout can have fewer keys.
    m_Highlight = std::min(m_Highlight, GetKeyCount() - 1);
    SyncParts();
}

void KeyboardComponent::Start()
{
    SyncParts();
    FocusSystem::Register(this);

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (!collider)
    {
        return;
    }
    collider->onPointerDown.push_back(
        [this](float x, float y)
        {
            if (!isEnabled || FocusSystem::IsInputBlocked(GetOwner()))
            {
                return;
            }
            m_Held = KeyAt(x, y);
            SyncParts();
        });
    collider->onPointerMove.push_back(
        [this](float x, float y)
        {
            if (m_Held >= 0)
            {
                // Sliding off a key onto another moves the press with it.
                const int32_t key = KeyAt(x, y);
                if (key >= 0 && key != m_Held)
                {
                    m_Held = key;
                    SyncParts();
                }
            }
        });
    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            const int32_t key = m_Held;
            m_Held = -1;
            if (key >= 0 && KeyAt(x, y) == key)
            {
                PressKey(key);
                // Typed by touch: the box shows its cursor.
                TextareaComponent* box = target.Get();
                if (box && !m_Focused)
                {
                    FocusSystem::SetFocus(box);
                }
            }
            SyncParts();
        });
}

void KeyboardComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void KeyboardComponent::RuntimeUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncParts();
}

void KeyboardComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    if (focused && m_Highlight < 0)
    {
        m_Highlight = 0;
    }
    SyncParts();
}

bool KeyboardComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    (void)character;
    namespace Keys = DekiInput::Keys;
    if (m_Highlight < 0)
    {
        m_Highlight = 0;
    }
    int32_t row = 0;
    int32_t column = 0;
    FindKey(mode, m_Highlight, &row, &column);
    int32_t next = -1;
    if (key == Keys::Left || key == Keys::Right)
    {
        const int32_t step = key == Keys::Right ? 1 : -1;
        const int32_t rowCount = RowsFor(mode)[row].count;
        if (column + step >= 0 && column + step < rowCount)
        {
            next = m_Highlight + step;
        }
    }
    else if (key == Keys::Up || key == Keys::Down)
    {
        next = KeyInRow(m_Highlight, key == Keys::Down ? 1 : -1);
    }
    else if (key == Keys::Enter || key == Keys::Space)
    {
        PressKey(m_Highlight);
        return true;
    }
    if (next < 0)
    {
        return false;  // off the edge: the focus moves on
    }
    m_Highlight = next;
    SyncParts();
    return true;
}

void KeyboardComponent::SyncParts()
{
    Deki::Object* owner = GetOwner();
    if (!owner)
    {
        return;
    }
    if (owner->GetChildren().size() != m_KnownChildren)
    {
        EnsureParts();
        m_KnownChildren = owner->GetChildren().size();
        m_LastLayout = 0;
    }
    DekiRendering::Fingerprint f;
    f.Add(width);
    f.Add(height);
    f.Add(static_cast<uint32_t>(mode));
    f.Add(Deki::EngineSettings::Global().pixelsPerMeter);
    const uint64_t capsKey = f.Value();
    f.Add(m_Highlight);
    f.Add(m_Held);
    f.Add(static_cast<uint32_t>(m_Focused));
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    // The caps bake only when the keys themselves change.
    const bool rebake = m_LastLayout == 0 || capsKey != m_CapsKey;
    m_LastLayout = f.Value();
    m_CapsKey = capsKey;
    if (rebake)
    {
        LayoutParts();
        BakeCaps();
    }
    PlacePress();
}

void KeyboardComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Caps = WidgetParts::ShapePart(owner, "Caps", ShapeKind::Custom, WidgetColors::Track);
    m_Specials = WidgetParts::ShapePart(owner, "Specials", ShapeKind::Custom, WidgetColors::Panel);
    m_Press = WidgetParts::ShapePart(
        owner, "Press", ShapeKind::Rectangle,
        Deki::Color(WidgetColors::Accent.r, WidgetColors::Accent.g, WidgetColors::Accent.b, 170));
    m_Labels.clear();
    for (int32_t i = 0; i < kMaxKeys; ++i)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "Key%d", static_cast<int>(i));
        Deki::Object* label = WidgetParts::TextPart(owner, name, WidgetColors::Text);
        if (!label)
        {
            break;  // on the device: the labels the scene has
        }
        m_Labels.push_back(label);
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
}

void KeyboardComponent::LayoutParts()
{
    const int32_t count = GetKeyCount();
    for (int32_t i = 0; i < static_cast<int32_t>(m_Labels.size()); ++i)
    {
        Deki::Object* label = m_Labels[i];
        label->SetActive(i < count);
        if (i >= count)
        {
            continue;
        }
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        KeyBox(i, x, y, w, h);
        WidgetParts::PlaceText(label, x, y, w, h);
        WidgetParts::SetPartText(label, GetKeyLabel(i));
    }
    WidgetParts::PlaceShape(m_Caps, 0.0f, 0.0f, width, height);
    WidgetParts::PlaceShape(m_Specials, 0.0f, 0.0f, width, height);

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (collider && inputCollider.GetObject() == GetOwner())
    {
        collider->width = width;
        collider->height = height;
    }
}

void KeyboardComponent::BakeCaps()
{
    ShapeComponent* caps = WidgetParts::Shape(m_Caps);
    ShapeComponent* specials = WidgetParts::Shape(m_Specials);
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t maskW = static_cast<int32_t>(std::lround(width * ppm));
    const int32_t maskH = static_cast<int32_t>(std::lround(height * ppm));
    if (maskW <= 0 || maskH <= 0)
    {
        return;
    }
    uint8_t* capsMask = caps ? caps->BeginCustomMask(maskW, maskH) : nullptr;
    uint8_t* specialMask = specials ? specials->BeginCustomMask(maskW, maskH) : nullptr;

    // Each key a rounded rectangle, a little inside its box, written into
    // its patch of the mask: one bake for the whole keyboard.
    const int32_t count = GetKeyCount();
    for (int32_t i = 0; i < count; ++i)
    {
        const KeyDef* key = FindKey(mode, i);
        uint8_t* mask = IsSpecial(key->action) ? specialMask : capsMask;
        if (!mask)
        {
            continue;
        }
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        KeyBox(i, x, y, w, h);
        const float gap = std::min(w, h) * 0.1f;
        const int32_t left = std::clamp<int32_t>(
            static_cast<int32_t>(std::lround((x - w * 0.5f + gap * 0.5f + width * 0.5f) * ppm)), 0, maskW);
        const int32_t right = std::clamp<int32_t>(
            static_cast<int32_t>(std::lround((x + w * 0.5f - gap * 0.5f + width * 0.5f) * ppm)), 0, maskW);
        const int32_t top = std::clamp<int32_t>(
            static_cast<int32_t>(std::lround((height * 0.5f - (y + h * 0.5f - gap * 0.5f)) * ppm)), 0, maskH);
        const int32_t bottom = std::clamp<int32_t>(
            static_cast<int32_t>(std::lround((height * 0.5f - (y - h * 0.5f + gap * 0.5f)) * ppm)), 0, maskH);
        if (right <= left || bottom <= top)
        {
            continue;
        }
        ShapeMaskParams params;
        params.kind = ShapeKind::Rectangle;
        params.width = static_cast<float>(right - left);
        params.height = static_cast<float>(bottom - top);
        params.cornerRadius = std::min(params.width, params.height) * 0.2f;
        BakeShapeMask(mask + static_cast<size_t>(top) * maskW + left, right - left, bottom - top, maskW, params);
    }
    if (caps)
    {
        caps->EndCustomMask();
    }
    if (specials)
    {
        specials->EndCustomMask();
    }
}

void KeyboardComponent::PlacePress()
{
    if (!m_Press)
    {
        return;
    }
    const int32_t key = m_Held >= 0 ? m_Held : (m_Focused ? m_Highlight : -1);
    m_Press->SetActive(key >= 0);
    if (key < 0)
    {
        return;
    }
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    KeyBox(key, x, y, w, h);
    const float gap = std::min(w, h) * 0.1f;
    WidgetParts::PlaceShape(m_Press, x, y, w - gap, h - gap);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Press))
    {
        shape->cornerRadius = std::min(w, h) * 0.18f;
    }
}

}  // namespace Deki2D
