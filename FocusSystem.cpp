#include "FocusSystem.h"

#include <deki/Object.h>
#include "deki-input/DekiInput.h"
#include "deki-input/Keys.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Deki2D
{

namespace
{

struct Scope
{
    Deki::Object* root = nullptr;
    IFocusable* owner = nullptr;
};

constexpr size_t kNoListener = static_cast<size_t>(-1);

std::vector<IFocusable*> s_Widgets;
std::vector<Scope> s_Scopes;
IFocusable* s_Focused = nullptr;
size_t s_Listener = kNoListener;
bool s_Wrap = true;

void Listen(bool on)
{
    if (on && s_Listener == kNoListener)
    {
        s_Listener = DekiInput::DekiInput::RegisterEventCallback(
            [](const DekiInput::InputEvent& event)
            {
                using DekiInput::InputEventType;
                if (event.type == InputEventType::KeyDown)
                {
                    FocusSystem::HandleKey(event.key, event.character);
                }
                else if (event.type == InputEventType::MouseButtonDown || event.type == InputEventType::TouchDown)
                {
                    FocusSystem::HandlePointerDown();
                }
            });
    }
    else if (!on && s_Listener != kNoListener)
    {
        DekiInput::DekiInput::UnregisterEventCallback(s_Listener);
        s_Listener = kNoListener;
    }
}

bool IsCandidate(IFocusable* widget)
{
    if (!widget || !widget->CanFocus())
    {
        return false;
    }
    Deki::Object* obj = widget->GetFocusObject();
    if (!obj || !obj->IsActiveInHierarchy())
    {
        return false;
    }
    if (!s_Scopes.empty())
    {
        Deki::Object* root = s_Scopes.back().root;
        if (obj != root && !obj->IsDescendantOf(root))
        {
            return false;
        }
    }
    return true;
}

// Reading order: top to bottom, then left to right. World Y is up.
bool ReadsBefore(IFocusable* a, IFocusable* b)
{
    const Deki::Object* oa = a->GetFocusObject();
    const Deki::Object* ob = b->GetFocusObject();
    const float ay = oa->GetWorldY();
    const float by = ob->GetWorldY();
    if (std::fabs(ay - by) > 1e-3f)
    {
        return ay > by;
    }
    return oa->GetWorldX() < ob->GetWorldX();
}

IFocusable* First()
{
    IFocusable* best = nullptr;
    for (IFocusable* w : s_Widgets)
    {
        if (IsCandidate(w) && (!best || ReadsBefore(w, best)))
        {
            best = w;
        }
    }
    return best;
}

// A widget's box: centre and half size, in world meters.
struct FocusBox
{
    float x = 0.0f;
    float y = 0.0f;
    float halfW = 0.0f;
    float halfH = 0.0f;
};

FocusBox BoxOf(IFocusable* widget)
{
    const Deki::Object* obj = widget->GetFocusObject();
    float w = 0.0f;
    float h = 0.0f;
    widget->GetFocusSize(w, h);
    FocusBox box;
    box.x = obj->GetWorldX();
    box.y = obj->GetWorldY();
    box.halfW = w * 0.5f * std::fabs(obj->GetWorldScaleX());
    box.halfH = h * 0.5f * std::fabs(obj->GetWorldScaleY());
    return box;
}

// The gap between two spans on one axis, 0 when they overlap.
float SpanGap(float a, float halfA, float b, float halfB)
{
    return std::max(0.0f, std::fabs(a - b) - halfA - halfB);
}

// The candidate nearest the focused widget along (dirX, dirY), one of the
// four arrow directions. Distance along the way counts once and distance off
// to the side twice, both from edge to edge, so a widget straight ahead
// beats a closer one off to the side; centre distance breaks ties. With
// nothing that way and wrapping on, the farthest one the other way, so a
// column comes round.
IFocusable* Nearest(float dirX, float dirY)
{
    const FocusBox from = BoxOf(s_Focused);
    const bool vertical = dirX == 0.0f;

    IFocusable* ahead = nullptr;
    float aheadScore = 0.0f;
    float aheadCentre = 0.0f;
    IFocusable* behind = nullptr;
    float behindScore = 0.0f;
    for (IFocusable* w : s_Widgets)
    {
        if (w == s_Focused || !IsCandidate(w))
        {
            continue;
        }
        const FocusBox to = BoxOf(w);
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float along = dx * dirX + dy * dirY;
        const float centre = std::fabs(dx) + std::fabs(dy);
        const float gapAlong =
            vertical ? SpanGap(from.y, from.halfH, to.y, to.halfH) : SpanGap(from.x, from.halfW, to.x, to.halfW);
        const float gapSide =
            vertical ? SpanGap(from.x, from.halfW, to.x, to.halfW) : SpanGap(from.y, from.halfH, to.y, to.halfH);
        if (along > 1e-3f)
        {
            const float score = gapAlong + gapSide * 2.0f;
            if (!ahead || score < aheadScore - 1e-4f || (score < aheadScore + 1e-4f && centre < aheadCentre))
            {
                ahead = w;
                aheadScore = score;
                aheadCentre = centre;
            }
        }
        else if (along < -1e-3f)
        {
            // Farthest back, then most in line.
            const float score = along + gapSide * 2.0f;
            if (!behind || score < behindScore)
            {
                behind = w;
                behindScore = score;
            }
        }
    }
    if (ahead)
    {
        return ahead;
    }
    return s_Wrap ? behind : nullptr;
}

IFocusable* NextInReadingOrder()
{
    IFocusable* next = nullptr;
    IFocusable* first = nullptr;
    for (IFocusable* w : s_Widgets)
    {
        if (w == s_Focused || !IsCandidate(w))
        {
            continue;
        }
        if (!first || ReadsBefore(w, first))
        {
            first = w;
        }
        if (ReadsBefore(s_Focused, w) && (!next || ReadsBefore(w, next)))
        {
            next = w;
        }
    }
    return next ? next : first;
}

}  // namespace

void FocusSystem::Register(IFocusable* widget)
{
    if (!widget || std::find(s_Widgets.begin(), s_Widgets.end(), widget) != s_Widgets.end())
    {
        return;
    }
    s_Widgets.push_back(widget);
    Listen(true);
}

void FocusSystem::Unregister(IFocusable* widget)
{
    if (s_Focused == widget)
    {
        // No OnFocusChanged: the widget is on its way out.
        s_Focused = nullptr;
    }
    for (Scope& scope : s_Scopes)
    {
        if (scope.owner == widget)
        {
            scope.owner = nullptr;
        }
    }
    auto it = std::find(s_Widgets.begin(), s_Widgets.end(), widget);
    if (it != s_Widgets.end())
    {
        s_Widgets.erase(it);
    }
    if (s_Widgets.empty())
    {
        s_Scopes.clear();
        Listen(false);
    }
}

IFocusable* FocusSystem::GetFocused()
{
    return s_Focused;
}

void FocusSystem::SetFocus(IFocusable* widget)
{
    if (widget == s_Focused)
    {
        return;
    }
    IFocusable* old = s_Focused;
    s_Focused = widget;
    if (old)
    {
        old->OnFocusChanged(false);
    }
    if (widget)
    {
        widget->OnFocusChanged(true);
    }
}

bool FocusSystem::HandleKey(uint32_t key, uint32_t character)
{
    namespace Keys = DekiInput::Keys;

    // The focused widget can have been hidden or disabled since.
    if (s_Focused && !IsCandidate(s_Focused))
    {
        SetFocus(nullptr);
    }

    if (s_Focused && s_Focused->OnFocusKey(key, character))
    {
        return true;
    }
    if (!s_Scopes.empty() && s_Scopes.back().owner && s_Scopes.back().owner != s_Focused &&
        s_Scopes.back().owner->OnFocusKey(key, character))
    {
        return true;
    }

    const bool arrow = key == Keys::Up || key == Keys::Down || key == Keys::Left || key == Keys::Right;
    if (!s_Focused)
    {
        // Enter is left alone: on a screen with a text field it types.
        if (arrow || key == Keys::Tab)
        {
            IFocusable* first = First();
            SetFocus(first);
            return first != nullptr;
        }
        return false;
    }

    IFocusable* next = nullptr;
    switch (key)
    {
        case Keys::Up: next = Nearest(0.0f, 1.0f); break;
        case Keys::Down: next = Nearest(0.0f, -1.0f); break;
        case Keys::Left: next = Nearest(-1.0f, 0.0f); break;
        case Keys::Right: next = Nearest(1.0f, 0.0f); break;
        case Keys::Tab: next = NextInReadingOrder(); break;
        default: return false;
    }
    if (next)
    {
        SetFocus(next);
    }
    // An arrow with nowhere to go is still used: it must not reach anything
    // else listening for keys while a widget has focus.
    return true;
}

void FocusSystem::HandlePointerDown()
{
    SetFocus(nullptr);
}

void FocusSystem::PushScope(Deki::Object* root, IFocusable* owner)
{
    if (!root)
    {
        return;
    }
    s_Scopes.push_back({ root, owner });
    // Focus outside the new scope goes.
    if (s_Focused && !IsCandidate(s_Focused))
    {
        SetFocus(nullptr);
    }
}

void FocusSystem::PopScope(Deki::Object* root)
{
    for (size_t i = 0; i < s_Scopes.size(); ++i)
    {
        if (s_Scopes[i].root == root)
        {
            s_Scopes.resize(i);
            break;
        }
    }
}

bool FocusSystem::IsInputBlocked(const Deki::Object* obj)
{
    if (s_Scopes.empty() || !obj)
    {
        return false;
    }
    Deki::Object* root = s_Scopes.back().root;
    return obj != root && !obj->IsDescendantOf(root);
}

void FocusSystem::SetWrap(bool wrap)
{
    s_Wrap = wrap;
}

size_t FocusSystem::GetWidgetCount()
{
    return s_Widgets.size();
}

}  // namespace Deki2D
