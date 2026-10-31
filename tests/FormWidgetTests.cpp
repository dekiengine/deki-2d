// The form widgets: the spinbox's buttons and repeat, the dropdown's list
// and the scope it holds while open, the tab view's pages, the dialog's
// buttons and focus scope, and the date picker's calendar.

#include <gtest/gtest.h>

#include "ButtonComponent.h"
#include "DatePickerComponent.h"
#include "DropdownComponent.h"
#include "FocusSystem.h"
#include "MsgboxComponent.h"
#include "SpinboxComponent.h"
#include "TabviewComponent.h"
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

constexpr float kTolerance = 1e-4f;

void Press(DekiInput::InputCollider* collider, float x, float y)
{
    collider->ProcessInput(x, y, true, false, false);
}

void Release(DekiInput::InputCollider* collider, float x, float y)
{
    collider->ProcessInput(x, y, false, false, true);
}

void Tap(DekiInput::InputCollider* collider, float x, float y)
{
    Press(collider, x, y);
    Release(collider, x, y);
}

std::string TextOf(Deki::Object* part)
{
    auto* text = part ? part->GetComponent<TextComponent>() : nullptr;
    return text ? text->text : std::string("<none>");
}

}  // namespace

TEST(Spinbox, ButtonsStepAndHoldingRepeats)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("Spinbox");
    auto* spinbox = root.AddComponent<SpinboxComponent>();
    spinbox->value = 5.0f;
    spinbox->SyncParts();
    spinbox->Start();
    auto* collider = root.GetComponent<DekiInput::InputCollider>();
    ASSERT_NE(collider, nullptr);
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Value")), "5");

    const float plusX = spinbox->width * 0.5f - 0.2f;
    Tap(collider, plusX, 0.0f);
    EXPECT_NEAR(spinbox->value, 6.0f, kTolerance);
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Value")), "6");
    Tap(collider, -plusX, 0.0f);
    EXPECT_NEAR(spinbox->value, 5.0f, kTolerance);
    Tap(collider, 0.0f, 0.0f);  // the number itself is no button
    EXPECT_NEAR(spinbox->value, 5.0f, kTolerance);

    // Held: one step at once, then a pause, then repeats.
    Press(collider, plusX, 0.0f);
    spinbox->RuntimeUpdate(0.3f);
    EXPECT_NEAR(spinbox->value, 6.0f, kTolerance);
    spinbox->RuntimeUpdate(0.3f);  // 0.6 s: past the pause, two repeats in
    EXPECT_NEAR(spinbox->value, 8.0f, kTolerance);
    Release(collider, plusX, 0.0f);
    spinbox->RuntimeUpdate(1.0f);
    EXPECT_NEAR(spinbox->value, 8.0f, kTolerance);
}

TEST(Spinbox, RangeDecimalsAndWrap)
{
    Deki::Object root("Spinbox");
    auto* spinbox = root.AddComponent<SpinboxComponent>();
    spinbox->minValue = 0.0f;
    spinbox->maxValue = 1.0f;
    spinbox->step = 0.25f;
    spinbox->decimals = 2;
    spinbox->value = 1.0f;
    spinbox->SyncParts();
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Value")), "1.00");
    EXPECT_TRUE(spinbox->OnFocusKey(Keys::Right, 0));
    EXPECT_NEAR(spinbox->value, 1.0f, kTolerance);  // held at the top
    spinbox->wrap = true;
    spinbox->StepBy(1);
    EXPECT_NEAR(spinbox->value, 0.0f, kTolerance);  // round to the bottom
    spinbox->OnFocusKey(Keys::Left, 0);
    EXPECT_NEAR(spinbox->value, 1.0f, kTolerance);
}

TEST(Dropdown, TapOpensAndARowPicks)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("Dropdown");
    auto* dropdown = root.AddComponent<DropdownComponent>();
    dropdown->options = { "Easy", "Normal", "Hard" };
    dropdown->SyncParts();
    dropdown->Start();
    Deki::Object* list = WidgetParts::Find(&root, "List");
    ASSERT_NE(list, nullptr);
    EXPECT_FALSE(list->IsActive());
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Label")), "Easy");

    int32_t picked = -1;
    dropdown->AddOnSelectionChangedCallback([&](int32_t index, const std::string&) { picked = index; });
    Tap(root.GetComponent<DekiInput::InputCollider>(), 0.0f, 0.0f);
    ASSERT_TRUE(dropdown->IsOpen());
    EXPECT_TRUE(list->IsActive());

    // While open, the rest of the scene is behind the list's scope.
    Deki::Object elsewhere("Elsewhere");
    EXPECT_TRUE(FocusSystem::IsInputBlocked(&elsewhere));

    // The third row, under the field.
    auto* rows = WidgetParts::Find(list, "Rows")->GetComponent<DekiInput::InputCollider>();
    const float rowY = list->GetWorldY() + dropdown->height * 1.5f - dropdown->height * 2.5f;
    Tap(rows, 0.0f, rowY);
    EXPECT_FALSE(dropdown->IsOpen());
    EXPECT_FALSE(list->IsActive());
    EXPECT_EQ(dropdown->selectedIndex, 2);
    EXPECT_EQ(picked, 2);
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Label")), "Hard");
    EXPECT_FALSE(FocusSystem::IsInputBlocked(&elsewhere));
}

TEST(Dropdown, KeysOpenMoveAndPick)
{
    Deki::Object root("Dropdown");
    auto* dropdown = root.AddComponent<DropdownComponent>();
    dropdown->options = { "A", "B", "C", "D", "E", "F" };
    dropdown->listRows = 3;
    dropdown->Start();
    FocusSystem::SetFocus(dropdown);
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Enter, 0));
    ASSERT_TRUE(dropdown->IsOpen());
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_TRUE(FocusSystem::HandleKey(Keys::Down, 0));
    }
    EXPECT_EQ(dropdown->GetHighlighted(), 4);
    // Scrolled so the highlight shows in the last of three rows.
    EXPECT_EQ(TextOf(WidgetParts::Find(WidgetParts::Find(&root, "List"), "Row2")), "E");
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Enter, 0));
    EXPECT_FALSE(dropdown->IsOpen());
    EXPECT_EQ(dropdown->GetSelectedValue(), "E");
    EXPECT_EQ(FocusSystem::GetFocused(), dropdown);  // the focus comes back

    FocusSystem::HandleKey(Keys::Enter, 0);
    FocusSystem::HandleKey(Keys::Up, 0);
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Esc, 0));
    EXPECT_FALSE(dropdown->IsOpen());
    EXPECT_EQ(dropdown->GetSelectedValue(), "E");  // Esc picks nothing
    FocusSystem::SetFocus(nullptr);
}

TEST(Tabview, OnePageShowsAtATime)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("Tabs");
    auto* tabs = root.AddComponent<TabviewComponent>();
    tabs->width = 12.0f;
    tabs->SyncParts();
    tabs->Start();
    ASSERT_NE(tabs->GetPage(2), nullptr);
    EXPECT_TRUE(tabs->GetPage(0)->IsActive());
    EXPECT_FALSE(tabs->GetPage(1)->IsActive());
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Tab1")), "Two");

    // A tap on the bar over the third tab.
    Deki::Object* bar = WidgetParts::Find(&root, "Bar");
    Tap(bar->GetComponent<DekiInput::InputCollider>(), 4.0f, bar->GetWorldY());
    EXPECT_EQ(tabs->selectedTab, 2);
    EXPECT_TRUE(tabs->GetPage(2)->IsActive());
    EXPECT_FALSE(tabs->GetPage(0)->IsActive());

    // Left steps back; past the first tab the focus moves on instead.
    EXPECT_TRUE(tabs->OnFocusKey(Keys::Left, 0));
    EXPECT_EQ(tabs->selectedTab, 1);
    EXPECT_TRUE(tabs->OnFocusKey(Keys::Left, 0));
    EXPECT_FALSE(tabs->OnFocusKey(Keys::Left, 0));
}

TEST(Msgbox, ButtonsCloseItAndSayWhich)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("Dialog");
    auto* box = root.AddComponent<MsgboxComponent>();
    box->buttons = { "Yes", "No", "Later" };
    box->SyncParts();
    box->Start();
    Deki::Object* panel = WidgetParts::Find(&root, "Box");
    ASSERT_NE(panel, nullptr);
    EXPECT_FALSE(panel->IsActive());

    int32_t closedWith = -2;
    box->AddOnClosedCallback([&](int32_t button) { closedWith = button; });
    box->Open();
    EXPECT_TRUE(panel->IsActive());
    Deki::Object outside("Outside");
    EXPECT_TRUE(FocusSystem::IsInputBlocked(&outside));

    Deki::Object* no = WidgetParts::Find(panel, "Button1");
    ASSERT_NE(no, nullptr);
    EXPECT_EQ(TextOf(WidgetParts::Find(no, "Label")), "No");
    auto* button = no->GetComponent<ButtonComponent>();
    button->Start();  // as the engine does once the box shows
    Tap(no->GetComponent<DekiInput::InputCollider>(), no->GetWorldX(), no->GetWorldY());
    EXPECT_EQ(closedWith, 1);
    EXPECT_FALSE(box->isOpen);
    EXPECT_FALSE(panel->IsActive());
    EXPECT_FALSE(FocusSystem::IsInputBlocked(&outside));
}

TEST(Msgbox, EscCancels)
{
    Deki::Object root("Dialog");
    auto* box = root.AddComponent<MsgboxComponent>();
    box->Start();
    int32_t closedWith = -2;
    box->AddOnClosedCallback([&](int32_t button) { closedWith = button; });
    box->Open();
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Esc, 0));
    EXPECT_EQ(closedWith, -1);

    box->closeOnEsc = false;
    box->Open();
    EXPECT_FALSE(FocusSystem::HandleKey(Keys::Esc, 0));
    EXPECT_TRUE(box->isOpen);
    box->Close();
}

TEST(DatePicker, MonthsHaveTheirDays)
{
    EXPECT_EQ(DatePickerComponent::DaysInMonth(2026, 1), 31);
    EXPECT_EQ(DatePickerComponent::DaysInMonth(2026, 4), 30);
    EXPECT_EQ(DatePickerComponent::DaysInMonth(2026, 2), 28);
    EXPECT_EQ(DatePickerComponent::DaysInMonth(2028, 2), 29);
    EXPECT_EQ(DatePickerComponent::DaysInMonth(2100, 2), 28);  // a century, not a leap year
    EXPECT_EQ(DatePickerComponent::DaysInMonth(2000, 2), 29);  // every 400 years it is
}

TEST(DatePicker, DatesStayReal)
{
    DatePickerComponent picker;
    picker.SetDate(2026, 2, 31);
    EXPECT_EQ(picker.day, 28);
    picker.SetDate(1990, 13, 0);
    EXPECT_EQ(picker.year, picker.minYear);
    EXPECT_EQ(picker.month, 12);
    EXPECT_EQ(picker.day, 1);
}
