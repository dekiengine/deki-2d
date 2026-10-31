#include "WidgetParts.h"

#include <deki/Object.h>

#include <cstring>

namespace Deki2D
{

namespace WidgetParts
{

Deki::Object* Find(Deki::Object* parent, const char* name)
{
    if (!parent || !name)
    {
        return nullptr;
    }
    for (Deki::Object* child : parent->GetChildren())
    {
        if (std::strcmp(child->GetName().c_str(), name) == 0)
        {
            return child;
        }
    }
    return nullptr;
}

Deki::Object* MakeChild(Deki::Object* parent, const char* name)
{
    if (!parent)
    {
        return nullptr;
    }
    auto* child = new Deki::Object(name);
    parent->AddChild(child);
    return child;
}

void ShowFocus(Deki::Object* owner, bool show)
{
    if (Deki::Object* ring = Find(owner, "Focus"))
    {
        ring->SetActive(show);
    }
}

}  // namespace WidgetParts

}  // namespace Deki2D
