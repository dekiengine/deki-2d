// Typing: the text area takes keys while it has the focus, keeps to its
// length in characters (not bytes), and stars a password; the on-screen
// keyboard types into it and changes layout; and the chart scrolls and
// draws its series.

#include <gtest/gtest.h>

#include "ChartComponent.h"
#include "FocusSystem.h"
#include "KeyboardComponent.h"
#include "ShapeComponent.h"
#include "TextComponent.h"
#include "TextareaComponent.h"
#include "WidgetParts.h"
#include "deki-input/InputCollider.h"
#include "deki-input/InputDispatch.h"
#include "deki-input/Keys.h"

#include <deki/Engine.h>
#include <deki/Object.h>

#include <cmath>
#include <cstring>
#include <string>

using namespace Deki2D;
namespace Keys = DekiInput::Keys;

namespace
{

std::string TextOf(Deki::Object* part)
{
    auto* text = part ? part->GetComponent<TextComponent>() : nullptr;
    return text ? text->text : std::string("<none>");
}

void Tap(DekiInput::InputCollider* collider, float x, float y)
{
    collider->ProcessInput(x, y, true, false, false);
    collider->ProcessInput(x, y, false, false, true);
}

}  // namespace

TEST(Textarea, TypesWhileFocused)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("Name");
    auto* area = root.AddComponent<TextareaComponent>();
    area->SyncParts();
    area->Start();
    EXPECT_TRUE(WidgetParts::Find(&root, "Placeholder")->IsActive());

    // A tap gives it the focus; keys then type.
    Tap(root.GetComponent<DekiInput::InputCollider>(), 0.0f, 0.0f);
    EXPECT_EQ(FocusSystem::GetFocused(), area);
    EXPECT_TRUE(FocusSystem::HandleKey('h', 'H'));
    EXPECT_TRUE(FocusSystem::HandleKey('i', 0));  // a desktop key with no character
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Space, ' '));
    EXPECT_EQ(area->text, "Hi ");
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Text")), "Hi ");
    EXPECT_FALSE(WidgetParts::Find(&root, "Placeholder")->IsActive());

    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Backspace, 0));
    EXPECT_EQ(area->text, "Hi");
    // Arrows are not the text area's: they move the focus on.
    EXPECT_FALSE(area->OnFocusKey(Keys::Up, 0));

    std::string submitted;
    area->AddOnSubmitCallback([&](const std::string& value) { submitted = value; });
    FocusSystem::HandleKey(Keys::Enter, 0);
    EXPECT_EQ(submitted, "Hi");
    EXPECT_EQ(area->text, "Hi");  // one line: Enter submits, adds nothing
    FocusSystem::SetFocus(nullptr);
}

TEST(Textarea, LengthCountsCharacters)
{
    TextareaComponent area;
    area.maxLength = 3;
    EXPECT_TRUE(area.InsertText("a\xC3\xA9\xE3\x81\x82z"));  // a, é, あ, z
    EXPECT_EQ(area.text, "a\xC3\xA9\xE3\x81\x82");           // three characters, six bytes
    EXPECT_FALSE(area.InsertText("b"));
    area.Backspace();  // the whole of あ
    EXPECT_EQ(area.text, "a\xC3\xA9");
    area.SetText("abcdef");
    EXPECT_EQ(area.text, "abc");
}

TEST(Textarea, NewLinesAndStars)
{
    Deki::Object root("Note");
    auto* area = root.AddComponent<TextareaComponent>();
    area->oneLine = false;
    area->password = true;
    area->maxLength = 0;
    area->SyncParts();
    area->InsertText("ab");
    area->Enter();
    area->InsertText("\xC3\xA9");
    EXPECT_EQ(area->text, "ab\n\xC3\xA9");
    EXPECT_EQ(TextOf(WidgetParts::Find(&root, "Text")), "****");  // one star a character
}

TEST(Keyboard, KeysTypeIntoTheTarget)
{
    Deki::Object root("Screen");
    auto* boxObj = new Deki::Object("Box");
    root.AddChild(boxObj);
    auto* area = boxObj->AddComponent<TextareaComponent>();
    area->maxLength = 0;
    auto* keysObj = new Deki::Object("Keys");
    root.AddChild(keysObj);
    auto* keyboard = keysObj->AddComponent<KeyboardComponent>();
    keyboard->target.Set(boxObj);
    keyboard->SyncParts();

    EXPECT_EQ(keyboard->GetKeyCount(), 31);
    EXPECT_STREQ(keyboard->GetKeyLabel(0), "q");
    keyboard->PressKey(0);   // q
    keyboard->PressKey(19);  // shift: the first key of the third row
    EXPECT_EQ(keyboard->mode, KeyboardMode::Upper);
    EXPECT_STREQ(keyboard->GetKeyLabel(0), "Q");
    keyboard->PressKey(0);
    keyboard->PressKey(29);  // space
    keyboard->PressKey(28);  // 123
    EXPECT_EQ(keyboard->mode, KeyboardMode::Symbols);
    keyboard->PressKey(0);   // 1
    keyboard->PressKey(27);  // backspace, two keys wide in this layout
    keyboard->PressKey(26);  // +
    EXPECT_EQ(area->text, "qQ +");
    EXPECT_EQ(TextOf(WidgetParts::Find(keysObj, "Key0")), "1");
}

TEST(Keyboard, PointsFindTheirKeys)
{
    Deki::Object root("Keys");
    auto* keyboard = root.AddComponent<KeyboardComponent>();
    keyboard->width = 10.0f;
    keyboard->height = 4.0f;
    // Top row, key widths of 1: the first key's middle is at (-4.5, 1.5).
    EXPECT_EQ(keyboard->KeyAt(-4.5f, 1.5f), 0);
    EXPECT_EQ(keyboard->KeyAt(4.5f, 1.5f), 9);
    // The second row starts half a key in.
    EXPECT_EQ(keyboard->KeyAt(-4.9f, 0.5f), -1);
    EXPECT_EQ(keyboard->KeyAt(-4.0f, 0.5f), 10);
    // The space bar, six keys wide, in the bottom row.
    EXPECT_EQ(keyboard->KeyAt(0.0f, -1.5f), 29);
}

TEST(Keyboard, ArrowsWalkTheKeys)
{
    Deki::Object root("Keys");
    auto* keyboard = root.AddComponent<KeyboardComponent>();
    keyboard->width = 10.0f;
    keyboard->height = 4.0f;
    keyboard->OnFocusChanged(true);
    EXPECT_EQ(keyboard->GetHighlighted(), 0);
    EXPECT_FALSE(keyboard->OnFocusKey(Keys::Left, 0));  // the edge: the focus moves on
    EXPECT_TRUE(keyboard->OnFocusKey(Keys::Right, 0));
    EXPECT_EQ(keyboard->GetHighlighted(), 1);
    // From w (-3.5) down to a (-4) and s (-3), as near as each other: the first.
    EXPECT_TRUE(keyboard->OnFocusKey(Keys::Down, 0));
    EXPECT_EQ(keyboard->GetHighlighted(), 10);
    EXPECT_TRUE(keyboard->OnFocusKey(Keys::Down, 0));
    EXPECT_TRUE(keyboard->OnFocusKey(Keys::Down, 0));  // to the bottom row
    EXPECT_FALSE(keyboard->OnFocusKey(Keys::Down, 0));
    keyboard->OnFocusChanged(false);
}

TEST(Chart, AddingPastTheCountScrolls)
{
    ChartComponent chart;
    chart.points.clear();
    chart.pointCount = 4;
    chart.Start();
    const float* storage = nullptr;
    for (int i = 1; i <= 10; ++i)
    {
        chart.AddPoint(static_cast<float>(i));
        if (i == 4)
        {
            storage = chart.points.data();
        }
    }
    ASSERT_EQ(chart.points.size(), 4u);
    EXPECT_EQ(chart.points.front(), 7.0f);
    EXPECT_EQ(chart.points.back(), 10.0f);
    EXPECT_EQ(chart.points.data(), storage);  // the same memory all along
}

TEST(Chart, SeriesIsDrawnWhereTheValuesAre)
{
    Deki::Object root("Chart");
    auto* chart = root.AddComponent<ChartComponent>();
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    chart->width = 64.0f / ppm;
    chart->height = 32.0f / ppm;
    chart->pointCount = 2;
    chart->points = { 0.0f, 100.0f };  // from the bottom left to the top right
    chart->SyncParts();
    auto* series = WidgetParts::Find(&root, "Series")->GetComponent<ShapeComponent>();
    QuadBlit::Source src;
    float px = 0;
    float py = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    ASSERT_TRUE(series->RenderContent(&root, src, px, py, r, g, b, a));
    ASSERT_EQ(src.width, 64);
    ASSERT_EQ(src.height, 32);
    auto at = [&](int x, int y) { return src.pixels[y * src.width + x]; };
    EXPECT_GT(at(32, 16), 128);  // the middle of the diagonal
    EXPECT_EQ(at(60, 28), 0);    // bottom right: under the line
    EXPECT_EQ(at(4, 3), 0);      // top left: over it

    chart->kind = ChartKind::Bars;
    chart->points = { 50.0f, 100.0f };
    chart->SyncParts();
    ASSERT_TRUE(series->RenderContent(&root, src, px, py, r, g, b, a));
    EXPECT_EQ(at(16, 4), 0);  // the first bar stops half way up
    EXPECT_EQ(at(16, 28), 255);
    EXPECT_EQ(at(48, 4), 255);  // the second reaches the top
}
