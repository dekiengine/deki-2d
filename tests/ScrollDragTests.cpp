// Which way a ScrollComponent's content moves under a drag: with the finger,
// in both directions, through the InputCollider the scroll listens to.
//
// The two directions place their items with opposite signs (world Y is up, so
// a vertical list stacks its items downward at -centre; a horizontal one
// stacks them rightward at +centre), so one drag sign for both moved a
// horizontal list against the finger. Momentum is built from the same drag
// deltas, so a flick that goes the wrong way is the same bug.

#include <gtest/gtest.h>

#include "ClipComponent.h"
#include "ScrollComponent.h"
#include "ScrollElement.h"
#include "deki-input/InputCollider.h"
#include "deki-input/InputDispatch.h"

#include <deki/Object.h>
#include <deki/reflection/ComponentFactory.h>

#include <string>

using namespace Deki2D;

namespace
{

constexpr float kViewport = 10.0f;  // a 10 m square viewport and hit box
constexpr float kItem = 4.0f;       // five 4 m items: 20 m of content, 10 m of travel
constexpr float kTolerance = 1e-4f;

class ScrollDrag : public ::testing::Test
{
protected:
    void SetUp() override { DekiInput::InputDispatch::ReleaseGesture(); }
    void TearDown() override { DekiInput::InputDispatch::ReleaseGesture(); }
};

// A scroll centred on the origin, its collider covering the viewport.
struct Scroll
{
    Deki::Object root{ "Scroll" };
    DekiInput::InputCollider* collider = nullptr;
    ScrollComponent* scroll = nullptr;
    Deki::Object* item = nullptr;  // the third item, in view for every drag below

    explicit Scroll(ScrollDirection direction, bool reverseDrag = false)
    {
        collider = root.AddComponent<DekiInput::InputCollider>();
        collider->width = kViewport;
        collider->height = kViewport;

        auto* clipObj = new Deki::Object("Clip");
        root.AddChild(clipObj);
        auto* clip = clipObj->AddComponent<ClipComponent>();
        clip->width = kViewport;
        clip->height = kViewport;

        for (int i = 0; i < 5; ++i)
        {
            auto* child = new Deki::Object("Item" + std::to_string(i));
            clipObj->AddChild(child);
            auto* element = child->AddComponent<ScrollElement>();
            element->width = kItem;
            element->height = kItem;
        }
        item = clipObj->GetChildren()[2];

        // ScrollComponent overloads Update (its physics step beside
        // Component's), which AddComponent<T>'s fallback entry cannot look
        // through, and this build has no reflection tables: the entry is
        // written out here instead.
        static const Deki::ComponentTypeInfo kScrollInfo =
            Deki::MakeHandWrittenTypeInfo<ScrollComponent>(/*declaresUpdate=*/true);
        scroll = new ScrollComponent();
        scroll->SetTypeInfo(&kScrollInfo);
        Deki::Internal::AttachComponent(root, scroll);
        scroll->direction = direction;
        scroll->reverseDrag = reverseDrag;
        scroll->inputCollider.Set(&root);
        scroll->Start();
    }

    bool Horizontal() const { return scroll->direction == ScrollDirection::Horizontal; }

    // The item's position along the scroll direction.
    float ItemPos() const { return Horizontal() ? item->GetX() : item->GetY(); }

    void Pointer(float along, bool down, bool move, bool up)
    {
        const float x = Horizontal() ? along : 0.0f;
        const float y = Horizontal() ? 0.0f : along;
        collider->ProcessInput(x, y, down, move, up);
    }

    // Presses at `from`, moves to `to` in 1 m steps and lifts there.
    void Drag(float from, float to)
    {
        Pointer(from, true, false, false);
        const float step = (to > from) ? 1.0f : -1.0f;
        for (float at = from + step; (step > 0.0f) ? at <= to : at >= to; at += step)
        {
            Pointer(at, false, true, false);
        }
        Pointer(to, false, false, true);
    }
};

}  // namespace

TEST_F(ScrollDrag, AHorizontalListFollowsTheFinger)
{
    Scroll s(ScrollDirection::Horizontal);
    const float start = s.ItemPos();

    s.Drag(3.0f, -1.0f);  // 4 m left
    EXPECT_NEAR(s.ItemPos(), start - 4.0f, kTolerance);

    s.Drag(-1.0f, 1.0f);  // 2 m back right
    EXPECT_NEAR(s.ItemPos(), start - 2.0f, kTolerance);
}

TEST_F(ScrollDrag, AVerticalListFollowsTheFinger)
{
    Scroll s(ScrollDirection::Vertical);
    const float start = s.ItemPos();

    s.Drag(-3.0f, 1.0f);  // 4 m up
    EXPECT_NEAR(s.ItemPos(), start + 4.0f, kTolerance);

    s.Drag(1.0f, -1.0f);  // 2 m back down
    EXPECT_NEAR(s.ItemPos(), start + 2.0f, kTolerance);
}

// From the middle of the range, so neither end's spring can move the list and
// only the momentum does.
TEST_F(ScrollDrag, AFlickKeepsGoingTheWayTheFingerWent)
{
    Scroll h(ScrollDirection::Horizontal);
    h.scroll->ScrollTo(5.0f, false);
    h.Drag(3.0f, -1.0f);
    const float hReleased = h.ItemPos();
    h.scroll->Update(1.0f / 60.0f);
    EXPECT_LT(h.ItemPos(), hReleased);

    Scroll v(ScrollDirection::Vertical);
    v.scroll->ScrollTo(5.0f, false);
    v.Drag(-3.0f, 1.0f);
    const float vReleased = v.ItemPos();
    v.scroll->Update(1.0f / 60.0f);
    EXPECT_GT(v.ItemPos(), vReleased);
}

TEST_F(ScrollDrag, ReverseDragMovesTheListAgainstTheFinger)
{
    Scroll h(ScrollDirection::Horizontal, true);
    const float hStart = h.ItemPos();
    h.Drag(-3.0f, 1.0f);  // 4 m right
    EXPECT_NEAR(h.ItemPos(), hStart - 4.0f, kTolerance);

    Scroll v(ScrollDirection::Vertical, true);
    const float vStart = v.ItemPos();
    v.Drag(3.0f, -1.0f);  // 4 m down
    EXPECT_NEAR(v.ItemPos(), vStart + 4.0f, kTolerance);
}
