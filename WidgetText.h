#pragma once

// Helpers for widget parts drawn by a TextComponent. Inline, so they come and
// go with the text feature.

#include <deki/Color.h>
#include <deki/Object.h>
#include "TextComponent.h"
#include "WidgetParts.h"

#include <string>

namespace Deki2D
{
namespace WidgetParts
{

/// The part's TextComponent, or nullptr.
inline TextComponent* Text(Deki::Object* part)
{
    return part ? part->GetComponent<TextComponent>() : nullptr;
}

#ifdef DEKI_EDITOR
/// The first text with a font under `obj`, depth first, skipping `skip`.
inline TextComponent* FindFontedText(Deki::Object* obj, const Deki::Object* skip)
{
    if (!obj || obj == skip)
    {
        return nullptr;
    }
    if (auto* tc = obj->GetComponent<TextComponent>())
    {
        if (!tc->font.guid.empty() || !tc->font.source.empty())
        {
            return tc;
        }
    }
    for (Deki::Object* child : obj->GetChildren())
    {
        if (TextComponent* found = FindFontedText(child, skip))
        {
            return found;
        }
    }
    return nullptr;
}
#endif

/// The text part `name`, made in the editor when missing: aligned `align`,
/// centred top to bottom, in `color`, with the font of the nearest text that
/// has one (first among the part's relatives, then further out), at
/// `fontSize` pixels.
inline Deki::Object* TextPart(Deki::Object* owner, const char* name, const Deki::Color& color,
                              TextAlign align = TextAlign::Center, int32_t fontSize = 16)
{
    bool created = false;
    Deki::Object* part = FindOrCreate<TextComponent>(owner, name, created);
    TextComponent* tc = created ? Text(part) : nullptr;
    if (!tc)
    {
        return part;
    }
    tc->color = color;
    tc->align = align;
    tc->verticalAlign = TextVerticalAlign::Middle;
#ifdef DEKI_EDITOR
    tc->fontSize = fontSize;
    Deki::Object* top = part;
    while (top->GetParent())
    {
        top = top->GetParent();
        if (TextComponent* found = FindFontedText(top, part))
        {
            tc->font = found->font;
            break;
        }
    }
#else
    (void)fontSize;
#endif
    return part;
}

/// Sizes and places a text part, when there is one.
inline void PlaceText(Deki::Object* part, float x, float y, float width, float height)
{
    if (!part)
    {
        return;
    }
    part->SetLocalPosition(x, y);
    if (TextComponent* tc = Text(part))
    {
        tc->width = width;
        tc->height = height;
    }
}

/// Sets a text part's string, only when it changed, so the text is not baked
/// again for nothing.
inline void SetPartText(Deki::Object* part, const std::string& value)
{
    TextComponent* tc = Text(part);
    if (tc && tc->text != value)
    {
        tc->SetText(value);
    }
}

}  // namespace WidgetParts
}  // namespace Deki2D
