// The toggles (Switch, Checkbox), the Arc dial, the LED and the Spinner: each
// makes its parts, follows taps through its InputCollider and keys through
// FocusSystem, and lays its parts out from its value.

#include <gtest/gtest.h>

#include "ArcComponent.h"
#include "CheckboxComponent.h"
#include "FocusSystem.h"
#include "LedComponent.h"
#include "ShapeComponent.h"
#include "SpinnerComponent.h"
#include "SwitchComponent.h"
#include "TextComponent.h"
#include "WidgetParts.h"
#include "deki-input/InputCollider.h"
#include "deki-input/InputDispatch.h"
#include "deki-input/Keys.h"

#include <deki/Object.h>

#include <cmath>

using namespace Deki2D;
namespace Keys = DekiInput::Keys;

namespace
{

constexpr float kTolerance = 1e-3f;
constexpr float kDegrees = 3.14159265f / 180.0f;

void Tap(DekiInput::InputCollider* collider, float x, float y)
{
    collider->ProcessInput(x, y, true, false, false);
    collider->ProcessInput(x, y, false, false, true);
}

}  // namespace

TEST(Switch, TapAndEnterFlipIt)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("Switch");
    auto* toggle = root.AddComponent<SwitchComponent>();
    toggle->SyncParts();
    toggle->Start();
    auto* collider = root.GetComponent<DekiInput::InputCollider>();
    ASSERT_NE(collider, nullptr);
    EXPECT_NEAR(collider->width, toggle->width, kTolerance);

    int calls = 0;
    bool last = false;
    toggle->AddOnToggledCallback(
        [&](bool on)
        {
            ++calls;
            last = on;
        });
    Tap(collider, 0.0f, 0.0f);
    EXPECT_TRUE(toggle->isOn);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(last);

    FocusSystem::SetFocus(toggle);
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Enter, 0));
    EXPECT_FALSE(toggle->isOn);
    EXPECT_EQ(calls, 2);
    FocusSystem::SetFocus(nullptr);
}

TEST(Switch, KnobSlidesToTheOtherEnd)
{
    Deki::Object root("Switch");
    auto* toggle = root.AddComponent<SwitchComponent>();
    toggle->Start();
    Deki::Object* knob = WidgetParts::Find(&root, "Knob");
    ASSERT_NE(knob, nullptr);
    const float offX = knob->GetX();
    EXPECT_LT(offX, 0.0f);

    toggle->SetOn(true);
    toggle->RuntimeUpdate(0.02f);
    EXPECT_GT(knob->GetX(), offX);  // on its way
    EXPECT_LT(knob->GetX(), -offX);
    for (int i = 0; i < 100; ++i)
    {
        toggle->RuntimeUpdate(0.02f);
    }
    EXPECT_NEAR(knob->GetX(), -offX, kTolerance);
}

TEST(Checkbox, TapTicksItAndShowsTheMark)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("Checkbox");
    auto* box = root.AddComponent<CheckboxComponent>();
    box->label = "Sound";
    box->SyncParts();
    box->Start();
    Deki::Object* check = WidgetParts::Find(&root, "Check");
    Deki::Object* label = WidgetParts::Find(&root, "Label");
    ASSERT_NE(check, nullptr);
    ASSERT_NE(label, nullptr);
    EXPECT_FALSE(check->IsActive());
    EXPECT_EQ(label->GetComponent<TextComponent>()->text, "Sound");

    Tap(root.GetComponent<DekiInput::InputCollider>(), 2.0f, 0.0f);  // on the label
    EXPECT_TRUE(box->isChecked);
    EXPECT_TRUE(check->IsActive());
    EXPECT_EQ(WidgetParts::Find(&root, "Box")->GetComponent<ShapeComponent>()->lineWidth, 0.0f);  // filled

    box->SetChecked(false);
    EXPECT_FALSE(check->IsActive());
}

TEST(Checkbox, DisabledIgnoresTaps)
{
    DekiInput::InputDispatch::ReleaseGesture();
    Deki::Object root("Checkbox");
    auto* box = root.AddComponent<CheckboxComponent>();
    box->SyncParts();
    box->Start();
    box->isEnabled = false;
    Tap(root.GetComponent<DekiInput::InputCollider>(), 0.0f, 0.0f);
    EXPECT_FALSE(box->isChecked);
}

TEST(Arc, PointsRoundTheRingGiveValues)
{
    Deki::Object root("Arc");
    auto* arc = root.AddComponent<ArcComponent>();
    // The default gauge: 225 degrees clockwise through 270.
    // Straight up is 135 degrees round: half way.
    EXPECT_NEAR(arc->ValueAtPoint(0.0f, 2.0f, 50.0f), 50.0f, 0.01f);
    // The start, lower left.
    EXPECT_NEAR(arc->ValueAtPoint(std::cos(225 * kDegrees), std::sin(225 * kDegrees), 10.0f), 0.0f, 0.01f);
    // Straight right is 225 degrees round: 5/6 of the way.
    EXPECT_NEAR(arc->ValueAtPoint(2.0f, 0.0f, 80.0f), 83.333f, 0.01f);
    // In the gap at the bottom a drag stays at the end it came from.
    EXPECT_NEAR(arc->ValueAtPoint(0.1f, -2.0f, 95.0f), 100.0f, 0.01f);
    EXPECT_NEAR(arc->ValueAtPoint(-0.1f, -2.0f, 5.0f), 0.0f, 0.01f);
}

TEST(Arc, IndicatorEndsAtTheValue)
{
    Deki::Object root("Arc");
    auto* arc = root.AddComponent<ArcComponent>();
    arc->value = 50.0f;
    arc->SyncParts();
    auto* indicator = WidgetParts::Find(&root, "Indicator")->GetComponent<ShapeComponent>();
    // Counter-clockwise from the value's end (90 degrees, up) to the start.
    EXPECT_NEAR(indicator->startAngle, 90 * kDegrees, 1e-4f);
    EXPECT_NEAR(indicator->endAngle, 225 * kDegrees, 1e-4f);
    Deki::Object* knob = WidgetParts::Find(&root, "Knob");
    EXPECT_NEAR(knob->GetX(), 0.0f, kTolerance);
    EXPECT_GT(knob->GetY(), 0.0f);
}

TEST(Arc, KeysStepIt)
{
    Deki::Object root("Arc");
    auto* arc = root.AddComponent<ArcComponent>();
    arc->value = 50.0f;
    arc->step = 10.0f;
    EXPECT_TRUE(arc->OnFocusKey(Keys::Right, 0));
    EXPECT_NEAR(arc->value, 60.0f, kTolerance);
    EXPECT_TRUE(arc->OnFocusKey(Keys::Left, 0));
    EXPECT_TRUE(arc->OnFocusKey(Keys::Left, 0));
    EXPECT_NEAR(arc->value, 40.0f, kTolerance);
    EXPECT_FALSE(arc->OnFocusKey(Keys::Up, 0));
}

TEST(Led, BrightnessShadesTheBodyAndTheGlow)
{
    Deki::Object root("Led");
    auto* led = root.AddComponent<LedComponent>();
    led->color = Deki::Color(200, 100, 50);
    led->SyncParts();
    auto* body = WidgetParts::Find(&root, "Body")->GetComponent<ShapeComponent>();
    Deki::Object* glow = WidgetParts::Find(&root, "Glow");
    EXPECT_EQ(body->color, Deki::Color(200, 100, 50));
    EXPECT_TRUE(glow->IsActive());

    led->SetOn(false);
    led->SyncParts();
    EXPECT_EQ(body->color, Deki::Color(40, 20, 10));  // a fifth: dark, still its colour
    EXPECT_FALSE(glow->IsActive());
}

TEST(Spinner, TurnsItsArcInPlay)
{
    Deki::Object root("Spinner");
    auto* spinner = root.AddComponent<SpinnerComponent>();
    spinner->turnsPerSecond = 1.0f;
    spinner->Start();
    Deki::Object* arc = WidgetParts::Find(&root, "Arc");
    ASSERT_NE(arc, nullptr);
    spinner->RuntimeUpdate(0.25f);  // a quarter turn, clockwise
    EXPECT_NEAR(arc->GetLocalRotation(), -kTwoPi * 0.25f, 1e-4f);
    spinner->isSpinning = false;
    spinner->RuntimeUpdate(0.25f);
    EXPECT_NEAR(arc->GetLocalRotation(), -kTwoPi * 0.25f, 1e-4f);
}
