#pragma once

#include <cstdint>

#include <deki/Color.h>
#include <deki/Object.h>

namespace Deki2D
{

/// The default colours a widget's parts get when the editor creates them.
/// After that they are ordinary properties on the parts, to restyle freely.
namespace WidgetColors
{
// Named values, used like enum values.
// NOLINTBEGIN(readability-identifier-naming)
inline const Deki::Color Track = Deki::Color(58, 63, 75);
inline const Deki::Color Accent = Deki::Color(64, 140, 255);
inline const Deki::Color Knob = Deki::Color(240, 242, 246);
inline const Deki::Color Panel = Deki::Color(34, 38, 48);
inline const Deki::Color Text = Deki::Color(230, 233, 240);
inline const Deki::Color DimText = Deki::Color(140, 146, 160);
inline const Deki::Color Focus = Deki::Color(255, 200, 64);
// NOLINTEND(readability-identifier-naming)
}  // namespace WidgetColors

/// Finding and making the child objects a widget draws with: a slider's
/// Track, Fill and Knob, a checkbox's Box and Label, and so on. Helpers for
/// shape and text parts are in WidgetShapes.h and WidgetText.h, so a build
/// without shapes or text leaves them out with their components.
///
/// A part is found by name. In the editor a missing part is made, with the
/// component it needs; on the device the scene already holds it, and a
/// part the user deleted stays deleted (the widget skips it).
namespace WidgetParts
{

/// The child of `parent` called `name`, or nullptr.
Deki::Object* Find(Deki::Object* parent, const char* name);

/// A new child of `parent` called `name`, with no components.
Deki::Object* MakeChild(Deki::Object* parent, const char* name);

/// The child called `name`. In the editor, one is made when missing, with a
/// component T, and `created` is set. Elsewhere it is only found.
template <typename T>
Deki::Object* FindOrCreate(Deki::Object* parent, const char* name, bool& created)
{
    created = false;
    if (Deki::Object* found = Find(parent, name))
    {
        return found;
    }
#ifdef DEKI_EDITOR
    Deki::Object* child = MakeChild(parent, name);
    if (child)
    {
        child->AddComponent<T>();
        created = true;
    }
    return child;
#else
    return nullptr;
#endif
}

/// The colour `t` of the way from `a` to `b`, `t` from 0 to 1.
inline Deki::Color LerpColor(const Deki::Color& a, const Deki::Color& b, float t)
{
    auto mix = [t](uint8_t from, uint8_t to)
    { return static_cast<uint8_t>(static_cast<float>(from) + (static_cast<float>(to) - from) * t + 0.5f); };
    return Deki::Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a));
}

/// Moves `shown` toward `target` at `speed` (the gap shrinks by that share a
/// second; 0 or below jumps), snapping when close. Returns the new value.
inline float Glide(float shown, float target, float speed, float deltaTime)
{
    if (shown < 0.0f || speed <= 0.0f)
    {
        return target;
    }
    const float step = deltaTime * speed;
    shown += (target - shown) * (step < 1.0f ? step : 1.0f);
    const float gap = target - shown;
    return (gap < 0.0005f && gap > -0.0005f) ? target : shown;
}

/// Shows or hides `obj`, telling its scene when that changes. Before engine
/// 0.22, Object::SetActive alone left the scene's list of live components as
/// it was, so the components under an object shown later (a tab's page, a
/// dialog's buttons) never started or updated.
void SetShown(Deki::Object* obj, bool show);

/// Shows or hides the part called "Focus", the ring a widget shows while it
/// has keyboard focus.
void ShowFocus(Deki::Object* owner, bool show);

}  // namespace WidgetParts

}  // namespace Deki2D
