#include "TextareaComponent.h"

#include <deki/Engine.h>
#include <deki/Object.h>
#include "BitmapFont.h"
#include "deki-input/InputCollider.h"
#include "deki-input/Keys.h"
#include "deki-rendering/Fingerprint.h"
#include "WidgetShapes.h"
#include "WidgetText.h"

#include <algorithm>
#include <cmath>

namespace Deki2D
{

namespace
{

// The cursor shows for half of each second while the box has the focus.
constexpr float kBlinkPeriod = 1.0f;

bool IsContinuationByte(char c)
{
    return (static_cast<uint8_t>(c) & 0xC0) == 0x80;
}

// Characters, not bytes: a UTF-8 sequence counts once.
int32_t CountCharacters(const std::string& value)
{
    int32_t count = 0;
    for (char c : value)
    {
        if (!IsContinuationByte(c))
        {
            ++count;
        }
    }
    return count;
}

uint32_t HashText(const std::string& value)
{
    uint32_t h = 2166136261u;
    for (char c : value)
    {
        h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    }
    return h;
}

}  // namespace

TextareaComponent::~TextareaComponent()
{
    FocusSystem::Unregister(this);
}

void TextareaComponent::Start()
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
            (void)x;
            (void)y;
            m_Pressed = isEnabled && !FocusSystem::IsInputBlocked(GetOwner());
        });
    // The focus is taken on release: a press anywhere first clears it.
    collider->onPointerUp.push_back(
        [this](float x, float y)
        {
            (void)x;
            (void)y;
            DekiInput::InputCollider* col = inputCollider.Get();
            if (m_Pressed && col && col->IsPointerInside())
            {
                FocusSystem::SetFocus(this);
            }
            m_Pressed = false;
        });
}

void TextareaComponent::SetText(const std::string& newText)
{
    text = newText;
    if (maxLength > 0)
    {
        // Cut at the start of the first character past the limit.
        int32_t count = 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (!IsContinuationByte(text[i]) && count++ == maxLength)
            {
                text.erase(i);
                break;
            }
        }
    }
    Changed();
}

bool TextareaComponent::InsertText(const char* value)
{
    if (!value || !*value || !isEnabled)
    {
        return false;
    }
    bool added = false;
    int32_t count = CountCharacters(text);
    for (const char* p = value; *p;)
    {
        // One character: a lead byte and its continuation bytes.
        const char* end = p + 1;
        while (*end && IsContinuationByte(*end))
        {
            ++end;
        }
        if (maxLength > 0 && count >= maxLength)
        {
            break;
        }
        text.append(p, end);
        ++count;
        added = true;
        p = end;
    }
    if (added)
    {
        Changed();
    }
    return added;
}

void TextareaComponent::Backspace()
{
    if (text.empty())
    {
        return;
    }
    size_t cut = text.size() - 1;
    while (cut > 0 && IsContinuationByte(text[cut]))
    {
        --cut;
    }
    text.erase(cut);
    Changed();
}

void TextareaComponent::Enter()
{
    if (oneLine)
    {
        for (const auto& callback : onSubmit)
        {
            callback(text);
        }
        return;
    }
    InsertText("\n");
}

void TextareaComponent::Changed()
{
    m_Blink = 0.0f;  // the cursor shows at once after typing
    SyncParts();
    for (const auto& callback : onTextChanged)
    {
        callback(text);
    }
}

void TextareaComponent::AddOnTextChangedCallback(const std::function<void(const std::string&)>& callback)
{
    onTextChanged.push_back(callback);
}

void TextareaComponent::AddOnSubmitCallback(const std::function<void(const std::string&)>& callback)
{
    onSubmit.push_back(callback);
}

void TextareaComponent::OnPropertyChanged(const char* propertyName)
{
    (void)propertyName;
    m_LastLayout = 0;
    SyncParts();
}

void TextareaComponent::RuntimeUpdate(float deltaTime)
{
    if (m_Focused)
    {
        m_Blink = std::fmod(m_Blink + deltaTime, kBlinkPeriod);
    }
    if (m_Cursor)
    {
        const bool visible = m_Focused && m_Blink < kBlinkPeriod * 0.5f;
        if (m_Cursor->IsActive() != visible)
        {
            m_Cursor->SetActive(visible);
        }
    }
    SyncParts();
}

void TextareaComponent::OnFocusChanged(bool focused)
{
    m_Focused = focused;
    m_Blink = 0.0f;
    WidgetParts::ShowFocus(GetOwner(), focused);
    if (m_Cursor)
    {
        m_Cursor->SetActive(focused);
    }
}

bool TextareaComponent::OnFocusKey(uint32_t key, uint32_t character)
{
    namespace Keys = DekiInput::Keys;
    if (!isEnabled)
    {
        return false;
    }
    if (key == Keys::Backspace || key == Keys::Delete)
    {
        Backspace();
        return true;
    }
    if (key == Keys::Enter)
    {
        Enter();
        return true;
    }
    // A desktop keyboard can give the key without the character it types.
    uint32_t typed = character;
    if (typed == 0 && key >= 32 && key < 127)
    {
        typed = key;
    }
    if (typed < 32 || typed == 127)
    {
        return false;  // arrows and the like: they move the focus
    }
    // The code point as UTF-8.
    char utf8[5] = {};
    if (typed < 0x80)
    {
        utf8[0] = static_cast<char>(typed);
    }
    else if (typed < 0x800)
    {
        utf8[0] = static_cast<char>(0xC0 | (typed >> 6));
        utf8[1] = static_cast<char>(0x80 | (typed & 0x3F));
    }
    else if (typed < 0x10000)
    {
        utf8[0] = static_cast<char>(0xE0 | (typed >> 12));
        utf8[1] = static_cast<char>(0x80 | ((typed >> 6) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | (typed & 0x3F));
    }
    else
    {
        utf8[0] = static_cast<char>(0xF0 | (typed >> 18));
        utf8[1] = static_cast<char>(0x80 | ((typed >> 12) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | ((typed >> 6) & 0x3F));
        utf8[3] = static_cast<char>(0x80 | (typed & 0x3F));
    }
    InsertText(utf8);
    return true;
}

void TextareaComponent::SyncParts()
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
    f.Add(HashText(text));
    f.Add(static_cast<uint32_t>(text.size()));
    f.Add(HashText(placeholder));
    f.Add(static_cast<uint32_t>(oneLine));
    f.Add(static_cast<uint32_t>(password));
    if (f.Value() == m_LastLayout)
    {
        return;
    }
    m_LastLayout = f.Value();
    LayoutParts();
}

void TextareaComponent::EnsureParts()
{
    Deki::Object* owner = GetOwner();
    m_Box = WidgetParts::ShapePart(owner, "Box", ShapeKind::Rectangle, WidgetColors::Panel, 0.4f);
    m_Text = WidgetParts::TextPart(owner, "Text", WidgetColors::Text, TextAlign::Left);
    m_Placeholder = WidgetParts::TextPart(owner, "Placeholder", WidgetColors::DimText, TextAlign::Left);
    m_Cursor = WidgetParts::ShapePart(owner, "Cursor", ShapeKind::Rectangle, WidgetColors::Accent);
    m_Focus = WidgetParts::ShapePart(owner, "Focus", ShapeKind::Rectangle, WidgetColors::Focus, 0.6f);
    if (m_Focus)
    {
        m_Focus->SetActive(m_Focused);
    }
    if (m_Cursor)
    {
        m_Cursor->SetActive(m_Focused);
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

void TextareaComponent::CursorPosition(float& x, float& y) const
{
    const float pad = std::min(height, 1.75f) * 0.3f;
    const float inner = std::max(0.0f, width - pad * 2.0f);
    x = -width * 0.5f + pad;
    y = 0.0f;
    TextComponent* tc = WidgetParts::Text(m_Text);
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    if (!tc || ppm <= 0.0f || m_Shown.empty())
    {
        if (!oneLine)
        {
            y = height * 0.5f - pad - std::min(height, 1.75f) * 0.35f;
        }
        return;
    }
    if (oneLine)
    {
        const float textWidth = static_cast<float>(tc->GetTextWidth()) / ppm;
        x += std::min(textWidth, inner);
        return;
    }
    // Several lines: after the last one, measured with the text's own font.
    const BitmapFont* font = tc->GetFont();
    const float lineHeight = static_cast<float>(tc->GetTextHeight()) / ppm;
    if (!font || lineHeight <= 0.0f)
    {
        return;
    }
    const std::vector<std::string> lines = tc->WrapTextWithFont(font);
    const std::string last = lines.empty() ? std::string() : lines.back();
    const float lastWidth =
        static_cast<float>(font->MeasureWidth(last.c_str()) * std::max<int32_t>(1, tc->pixelScale)) / ppm;
    x += std::min(lastWidth, inner);
    y = height * 0.5f - pad - lineHeight * (static_cast<float>(std::max<size_t>(1, lines.size())) - 0.5f);
}

void TextareaComponent::LayoutParts()
{
    const float pad = std::min(height, 1.75f) * 0.3f;
    const float inner = std::max(0.0f, width - pad * 2.0f);
    // One line is centred in the whole height, so tall glyphs keep their
    // tops; several lines start a pad below the top.
    const float innerHeight = oneLine ? height : std::max(0.0f, height - pad * 2.0f);
    WidgetParts::PlaceShape(m_Box, 0.0f, 0.0f, width, height);

    if (password)
    {
        m_Shown.assign(static_cast<size_t>(CountCharacters(text)), '*');
    }
    else
    {
        m_Shown = text;
    }
    WidgetParts::PlaceText(m_Text, 0.0f, 0.0f, inner, innerHeight);
    WidgetParts::PlaceText(m_Placeholder, 0.0f, 0.0f, inner, innerHeight);
    WidgetParts::SetPartText(m_Text, m_Shown);
    WidgetParts::SetPartText(m_Placeholder, placeholder);
    if (m_Placeholder)
    {
        m_Placeholder->SetActive(text.empty());
    }
    const TextVerticalAlign vertical = oneLine ? TextVerticalAlign::Middle : TextVerticalAlign::Top;
    for (Deki::Object* part : { m_Text, m_Placeholder })
    {
        if (TextComponent* tc = WidgetParts::Text(part))
        {
            tc->verticalAlign = vertical;
        }
    }
    // One line wider than the box shows its end, where the typing is.
    if (TextComponent* tc = WidgetParts::Text(m_Text))
    {
        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const bool overflows = oneLine && ppm > 0.0f && static_cast<float>(tc->GetTextWidth()) / ppm > inner;
        tc->align = overflows ? TextAlign::Right : TextAlign::Left;
    }

    float cursorX = 0.0f;
    float cursorY = 0.0f;
    CursorPosition(cursorX, cursorY);
    const float cursorHeight = std::min(height, 1.75f) * 0.6f;
    WidgetParts::PlaceShape(m_Cursor, cursorX + 0.06f, cursorY, 0.12f, cursorHeight);

    const float ring = std::min(height, 1.75f) * 0.3f;
    WidgetParts::PlaceShape(m_Focus, 0.0f, 0.0f, width + ring, height + ring);
    if (ShapeComponent* shape = WidgetParts::Shape(m_Focus))
    {
        shape->lineWidth = std::min(height, 1.75f) * 0.08f;
    }

    DekiInput::InputCollider* collider = inputCollider.Get();
    if (collider && inputCollider.GetObject() == GetOwner())
    {
        collider->width = width;
        collider->height = height;
    }
}

}  // namespace Deki2D
