// A bar and a slider lay out their parts from the value, a slider takes its
// value from a drag through its InputCollider and from the arrow keys, and a
// bar can glide to a new value instead of jumping.

#include <gtest/gtest.h>

#include "BarComponent.h"
#include "FocusSystem.h"
#include "ShapeComponent.h"
#include "SliderComponent.h"
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

struct SliderRig
{
    Deki::Object root{ "Slider" };
    SliderComponent* slider = nullptr;
    DekiInput::InputCollider* collider = nullptr;

    SliderRig()
    {
        slider = root.AddComponent<SliderComponent>();
        slider->width = 10.0f;
        slider->height = 1.0f;
        slider->knobSize = 2.0f;
        // The editor build makes the parts and the collider on the first sync.
        slider->SyncParts();
        collider = root.GetComponent<DekiInput::InputCollider>();
        slider->Start();
    }

    Deki::Object* Part(const char* name) { return WidgetParts::Find(&root, name); }

    void Press(float x) { collider->ProcessInput(x, 0.0f, true, false, false); }
    void Move(float x) { collider->ProcessInput(x, 0.0f, false, true, false); }
    void Release(float x) { collider->ProcessInput(x, 0.0f, false, false, true); }
};

}  // namespace

TEST(Bar, FillGrowsFromTheLeftToTheValue)
{
    Deki::Object root("Bar");
    auto* bar = root.AddComponent<BarComponent>();
    bar->width = 10.0f;
    bar->height = 1.0f;
    bar->value = 25.0f;
    bar->SyncParts();

    Deki::Object* fill = WidgetParts::Find(&root, "Fill");
    Deki::Object* track = WidgetParts::Find(&root, "Track");
    ASSERT_NE(fill, nullptr);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(fill->GetComponent<ShapeComponent>()->width, 2.5f, kTolerance);
    EXPECT_NEAR(fill->GetX(), -5.0f + 1.25f, kTolerance);
    EXPECT_NEAR(track->GetComponent<ShapeComponent>()->width, 10.0f, kTolerance);
}

TEST(Bar, VerticalFillGrowsFromTheBottom)
{
    Deki::Object root("Bar");
    auto* bar = root.AddComponent<BarComponent>();
    bar->orientation = BarOrientation::Vertical;
    bar->width = 1.0f;
    bar->height = 8.0f;
    bar->value = 50.0f;
    bar->SyncParts();
    Deki::Object* fill = WidgetParts::Find(&root, "Fill");
    ASSERT_NE(fill, nullptr);
    EXPECT_NEAR(fill->GetComponent<ShapeComponent>()->height, 4.0f, kTolerance);
    EXPECT_NEAR(fill->GetY(), -2.0f, kTolerance);
}

TEST(Bar, GlidesToANewValue)
{
    Deki::Object root("Bar");
    auto* bar = root.AddComponent<BarComponent>();
    bar->value = 0.0f;
    bar->glideSpeed = 10.0f;
    bar->Start();
    EXPECT_NEAR(bar->GetShownFraction(), 0.0f, kTolerance);

    bar->SetValue(100.0f);
    bar->RuntimeUpdate(0.05f);
    EXPECT_GT(bar->GetShownFraction(), 0.1f);
    EXPECT_LT(bar->GetShownFraction(), 0.9f);
    for (int i = 0; i < 200; ++i)
    {
        bar->RuntimeUpdate(0.016f);
    }
    EXPECT_NEAR(bar->GetShownFraction(), 1.0f, kTolerance);
}

TEST(Bar, ValueIsKeptInRange)
{
    BarComponent bar;
    bar.SetValue(150.0f);
    EXPECT_EQ(bar.value, 100.0f);
    bar.SetValue(-3.0f);
    EXPECT_EQ(bar.value, 0.0f);
}

TEST(Slider, MakesItsPartsAndHitArea)
{
    SliderRig rig;
    EXPECT_NE(rig.Part("Track"), nullptr);
    EXPECT_NE(rig.Part("Fill"), nullptr);
    EXPECT_NE(rig.Part("Knob"), nullptr);
    ASSERT_NE(rig.Part("Focus"), nullptr);
    EXPECT_FALSE(rig.Part("Focus")->IsActive());
    ASSERT_NE(rig.collider, nullptr);
    // Track plus half a knob at each end.
    EXPECT_NEAR(rig.collider->width, 12.0f, kTolerance);
    EXPECT_NEAR(rig.collider->height, 2.0f, kTolerance);
}

TEST(Slider, KnobSitsAtTheValue)
{
    SliderRig rig;
    rig.slider->value = 75.0f;
    rig.slider->OnPropertyChanged("value");
    EXPECT_NEAR(rig.Part("Knob")->GetX(), 2.5f, kTolerance);
}

TEST(Slider, DragSetsTheValue)
{
    DekiInput::InputDispatch::ReleaseGesture();
    SliderRig rig;
    float changed = -1.0f;
    float committed = -1.0f;
    rig.slider->AddOnValueChangedCallback([&](float v) { changed = v; });
    rig.slider->AddOnValueCommittedCallback([&](float v) { committed = v; });

    rig.Press(-5.0f);  // the left end
    EXPECT_NEAR(rig.slider->value, 0.0f, kTolerance);
    EXPECT_NEAR(changed, 0.0f, kTolerance);
    rig.Move(2.5f);
    EXPECT_NEAR(rig.slider->value, 75.0f, kTolerance);
    EXPECT_NEAR(rig.Part("Knob")->GetX(), 2.5f, kTolerance);
    EXPECT_LT(committed, 0.0f);  // not until the finger lifts
    rig.Move(9.0f);              // past the end: held at the maximum
    EXPECT_NEAR(rig.slider->value, 100.0f, kTolerance);
    rig.Release(9.0f);
    EXPECT_NEAR(committed, 100.0f, kTolerance);
}

TEST(Slider, StepsSnapTheValue)
{
    SliderRig rig;
    rig.slider->step = 10.0f;
    rig.Press(1.4f);  // 64%
    EXPECT_NEAR(rig.slider->value, 60.0f, kTolerance);
    rig.Release(1.4f);
}

TEST(Slider, ArrowKeysStepItWhileFocused)
{
    SliderRig rig;
    rig.slider->value = 50.0f;
    FocusSystem::SetFocus(rig.slider);
    EXPECT_TRUE(rig.Part("Focus")->IsActive());

    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Right, 0));
    EXPECT_NEAR(rig.slider->value, 55.0f, kTolerance);  // a twentieth of the range
    rig.slider->keyStep = 20.0f;
    FocusSystem::HandleKey(Keys::Left, 0);
    EXPECT_NEAR(rig.slider->value, 35.0f, kTolerance);
    // Up and Down are not the slider's: they move the focus on.
    EXPECT_FALSE(rig.slider->OnFocusKey(Keys::Up, 0));

    FocusSystem::SetFocus(nullptr);
    EXPECT_FALSE(rig.Part("Focus")->IsActive());
}

TEST(Slider, DisabledIgnoresInput)
{
    SliderRig rig;
    rig.slider->isEnabled = false;
    rig.Press(4.0f);
    EXPECT_NEAR(rig.slider->value, 50.0f, kTolerance);
    EXPECT_FALSE(rig.slider->CanFocus());
    rig.Release(4.0f);
}
