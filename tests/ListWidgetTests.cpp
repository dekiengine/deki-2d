// The list widget: taps click rows and skip titles, a drag scrolls and does
// not click, and the arrows move past titles and scroll the highlight into view.

#include <gtest/gtest.h>

#include "FocusSystem.h"
#include "ListComponent.h"
#include "TextComponent.h"
#include "WidgetParts.h"
#include "deki-input/InputCollider.h"
#include "deki-input/InputDispatch.h"
#include "deki-input/Keys.h"

#include <deki/Object.h>

using namespace Deki2D;
namespace Keys = DekiInput::Keys;

namespace
{

DekiInput::InputCollider* RowsOf(Deki::Object& root)
{
    Deki::Object* rows = WidgetParts::Find(&root, "Rows");
    return rows ? rows->GetComponent<DekiInput::InputCollider>() : nullptr;
}

// The world y of row `row`'s middle.
float RowY(const ListComponent* list, int32_t row)
{
    return list->ListHeight() * 0.5f - list->rowHeight * (static_cast<float>(row) + 0.5f);
}

std::string TextOf(Deki::Object* part)
{
    auto* text = part ? part->GetComponent<TextComponent>() : nullptr;
    return text ? text->text : std::string("<none>");
}

}  // namespace

TEST(List, TapClicksARowAndNotATitle)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("List");
    auto* list = root.AddComponent<ListComponent>();
    list->items = { "# Audio", "Music", "Sound" };
    list->visibleRows = 3;
    list->SyncParts();
    list->Start();
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Row0")), "Audio");  // shown without its "# "

    int32_t clicked = -1;
    std::string text;
    list->AddOnItemClickedCallback(
        [&](int32_t index, const std::string& value)
        {
            clicked = index;
            text = value;
        });
    DekiInput::InputCollider* rows = RowsOf(root);
    ASSERT_NE(rows, nullptr);

    rows->ProcessInput(0.0f, RowY(list, 0), true, false, false);
    rows->ProcessInput(0.0f, RowY(list, 0), false, false, true);
    EXPECT_EQ(clicked, -1);  // a title

    rows->ProcessInput(0.0f, RowY(list, 2), true, false, false);
    rows->ProcessInput(0.0f, RowY(list, 2), false, false, true);
    EXPECT_EQ(clicked, 2);
    EXPECT_EQ(text, "Sound");
    EXPECT_EQ(list->GetHighlighted(), 2);
}

TEST(List, DragScrollsAndDoesNotClick)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("List");
    auto* list = root.AddComponent<ListComponent>();
    list->items = { "A", "B", "C", "D", "E", "F", "G", "H" };
    list->visibleRows = 3;
    list->SyncParts();
    list->Start();
    int32_t clicked = -1;
    list->AddOnItemClickedCallback([&](int32_t index, const std::string&) { clicked = index; });
    DekiInput::InputCollider* rows = RowsOf(root);
    ASSERT_NE(rows, nullptr);

    // Up by two rows: the items two further down show.
    const float y = RowY(list, 2);
    rows->ProcessInput(0.0f, y, true, false, false);
    rows->ProcessInput(0.0f, y + list->rowHeight * 2.0f, false, true, false);
    rows->ProcessInput(0.0f, y + list->rowHeight * 2.0f, false, false, true);
    EXPECT_EQ(list->GetScroll(), 2);
    EXPECT_EQ(clicked, -1);
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Row0")), "C");

    // Never past the last item.
    list->ScrollTo(100);
    EXPECT_EQ(list->GetScroll(), 5);
    EXPECT_TRUE(WidgetParts::Find(&root, "Thumb")->IsActive());
}

TEST(List, ArrowsSkipTitlesAndScroll)
{
    Deki::Object root("List");
    auto* list = root.AddComponent<ListComponent>();
    list->items = { "# One", "a", "b", "# Two", "c", "d" };
    list->visibleRows = 3;
    list->Start();
    FocusSystem::SetFocus(list);
    EXPECT_EQ(list->GetHighlighted(), 1);  // focus starts on the first row that is not a title

    int32_t clicked = -1;
    list->AddOnItemClickedCallback([&](int32_t index, const std::string&) { clicked = index; });
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Down, 0));
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Down, 0));
    EXPECT_EQ(list->GetHighlighted(), 4);  // past "# Two"
    // Scrolled just enough that the highlight shows, its title above it.
    EXPECT_EQ(list->GetScroll(), 2);
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Row1")), "Two");
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Row2")), "c");
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Enter, 0));
    EXPECT_EQ(clicked, 4);

    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Down, 0));
    EXPECT_FALSE(list->OnFocusKey(Keys::Down, 0));  // the end: the focus may move on
    FocusSystem::SetFocus(nullptr);
}
