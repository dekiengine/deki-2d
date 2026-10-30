// How the arrow keys, Enter and Tab move focus between widgets: nothing is
// focused until a key is pressed, an arrow finds the nearest widget that way
// and wraps round, a widget can keep the keys it uses, and a dialog's scope
// keeps the focus inside it.

#include <gtest/gtest.h>

#include "FocusSystem.h"
#include "deki-input/Keys.h"

#include <deki/Object.h>

#include <memory>
#include <string>
#include <vector>

using namespace Deki2D;
namespace Keys = DekiInput::Keys;

namespace
{

// A widget on its own object, which keeps the keys listed in `keeps`.
class FakeWidget : public IFocusable
{
public:
    FakeWidget(Deki::Object* parent, const char* name, float x, float y)
        : m_Obj(new Deki::Object(name))
    {
        parent->AddChild(m_Obj);
        m_Obj->SetLocalPosition(x, y);
        FocusSystem::Register(this);
    }
    ~FakeWidget() override { FocusSystem::Unregister(this); }

    Deki::Object* GetFocusObject() const override { return m_Obj; }
    bool CanFocus() const override { return enabled; }
    void GetFocusSize(float& w, float& h) const override
    {
        w = width;
        h = height;
    }
    void OnFocusChanged(bool focused) override { hasFocus = focused; }
    bool OnFocusKey(uint32_t key, uint32_t character) override
    {
        (void)character;
        lastKey = key;
        for (uint32_t k : keeps)
        {
            if (k == key)
            {
                return true;
            }
        }
        return false;
    }

    Deki::Object* Object() const { return m_Obj; }

    bool enabled = true;
    bool hasFocus = false;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t lastKey = 0;
    std::vector<uint32_t> keeps;

private:
    Deki::Object* m_Obj;  // owned by the parent
};

class Focus : public ::testing::Test
{
protected:
    void SetUp() override { FocusSystem::SetWrap(true); }
    void TearDown() override
    {
        widgets.clear();
        EXPECT_EQ(FocusSystem::GetWidgetCount(), 0u);
        EXPECT_EQ(FocusSystem::GetFocused(), nullptr);
    }

    FakeWidget* Add(const char* name, float x, float y)
    {
        widgets.push_back(std::make_unique<FakeWidget>(&root, name, x, y));
        return widgets.back().get();
    }

    // Declared before the widgets, so it outlives them.
    Deki::Object root{ "Root" };
    std::vector<std::unique_ptr<FakeWidget>> widgets;
};

}  // namespace

TEST_F(Focus, NothingIsFocusedUntilAKeyIsPressed)
{
    FakeWidget* top = Add("Top", 0, 5);
    Add("Bottom", 0, -5);
    EXPECT_EQ(FocusSystem::GetFocused(), nullptr);
    EXPECT_FALSE(top->hasFocus);

    // The first key picks the first widget in reading order, without moving.
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Down, 0));
    EXPECT_EQ(FocusSystem::GetFocused(), top);
    EXPECT_TRUE(top->hasFocus);
}

TEST_F(Focus, ArrowsMoveAlongAColumnAndWrap)
{
    FakeWidget* a = Add("A", 0, 4);
    FakeWidget* b = Add("B", 0, 0);
    FakeWidget* c = Add("C", 0, -4);
    FocusSystem::SetFocus(a);

    FocusSystem::HandleKey(Keys::Down, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), b);
    EXPECT_FALSE(a->hasFocus);
    EXPECT_TRUE(b->hasFocus);
    FocusSystem::HandleKey(Keys::Down, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), c);
    FocusSystem::HandleKey(Keys::Down, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), a);  // round to the top
    FocusSystem::HandleKey(Keys::Up, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), c);

    FocusSystem::SetFocus(nullptr);
}

TEST_F(Focus, WithoutWrapTheFocusStaysAtTheEnd)
{
    FocusSystem::SetWrap(false);
    FakeWidget* a = Add("A", 0, 4);
    Add("B", 0, 0);
    FocusSystem::SetFocus(a);
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Up, 0));
    EXPECT_EQ(FocusSystem::GetFocused(), a);
    FocusSystem::SetFocus(nullptr);
}

TEST_F(Focus, AWidgetStraightAheadBeatsACloserOneOffToTheSide)
{
    FakeWidget* from = Add("From", 0, 0);
    FakeWidget* ahead = Add("Ahead", 6, 0);
    Add("Aside", 3, 3);
    FocusSystem::SetFocus(from);
    FocusSystem::HandleKey(Keys::Right, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), ahead);
    FocusSystem::SetFocus(nullptr);
}

TEST_F(Focus, TheFocusedWidgetKeepsTheKeysItUses)
{
    FakeWidget* slider = Add("Slider", 0, 4);
    FakeWidget* below = Add("Below", 0, -4);
    slider->keeps = { Keys::Left, Keys::Right };
    FocusSystem::SetFocus(slider);

    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Right, 0));
    EXPECT_EQ(slider->lastKey, Keys::Right);
    EXPECT_EQ(FocusSystem::GetFocused(), slider);

    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Down, 0));
    EXPECT_EQ(FocusSystem::GetFocused(), below);
    FocusSystem::SetFocus(nullptr);
}

TEST_F(Focus, HiddenAndDisabledWidgetsAreSkipped)
{
    FakeWidget* a = Add("A", 0, 6);
    FakeWidget* hidden = Add("Hidden", 0, 3);
    FakeWidget* disabled = Add("Disabled", 0, 0);
    FakeWidget* d = Add("D", 0, -3);
    hidden->Object()->SetActive(false);
    disabled->enabled = false;
    FocusSystem::SetFocus(a);
    FocusSystem::HandleKey(Keys::Down, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), d);
    FocusSystem::SetFocus(nullptr);
}

TEST_F(Focus, TabFollowsReadingOrder)
{
    FakeWidget* topLeft = Add("TopLeft", -4, 4);
    FakeWidget* topRight = Add("TopRight", 4, 4);
    FakeWidget* bottom = Add("Bottom", 0, -4);
    FocusSystem::HandleKey(Keys::Tab, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), topLeft);
    FocusSystem::HandleKey(Keys::Tab, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), topRight);
    FocusSystem::HandleKey(Keys::Tab, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), bottom);
    FocusSystem::HandleKey(Keys::Tab, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), topLeft);
    FocusSystem::SetFocus(nullptr);
}

TEST_F(Focus, AScopeKeepsTheFocusInside)
{
    FakeWidget* outside = Add("Outside", 0, 6);
    auto* dialog = new Deki::Object("Dialog");
    root.AddChild(dialog);
    widgets.push_back(std::make_unique<FakeWidget>(dialog, "Ok", -2, 0));
    FakeWidget* ok = widgets.back().get();
    widgets.push_back(std::make_unique<FakeWidget>(dialog, "Cancel", 2, 0));
    FakeWidget* cancel = widgets.back().get();

    FocusSystem::SetFocus(outside);
    FocusSystem::PushScope(dialog, nullptr);
    EXPECT_EQ(FocusSystem::GetFocused(), nullptr);  // the focus outside went

    EXPECT_FALSE(FocusSystem::HandleKey(Keys::Enter, 0));  // Enter alone starts nothing
    EXPECT_EQ(FocusSystem::GetFocused(), nullptr);
    FocusSystem::HandleKey(Keys::Down, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), ok);
    FocusSystem::HandleKey(Keys::Up, 0);  // nothing above inside the scope
    EXPECT_EQ(FocusSystem::GetFocused(), ok);
    FocusSystem::HandleKey(Keys::Right, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), cancel);

    FocusSystem::PopScope(dialog);
    FocusSystem::HandleKey(Keys::Up, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), outside);
    FocusSystem::SetFocus(nullptr);
}

TEST_F(Focus, TheScopeOwnerGetsTheKeysTheWidgetLeaves)
{
    auto* dialog = new Deki::Object("Dialog");
    root.AddChild(dialog);
    widgets.push_back(std::make_unique<FakeWidget>(&root, "Owner", 0, 0));
    FakeWidget* owner = widgets.back().get();
    owner->keeps = { Keys::Esc };
    widgets.push_back(std::make_unique<FakeWidget>(dialog, "Button", 0, 0));
    FakeWidget* button = widgets.back().get();

    FocusSystem::PushScope(dialog, owner);
    FocusSystem::SetFocus(button);
    EXPECT_TRUE(FocusSystem::HandleKey(Keys::Esc, 0));
    EXPECT_EQ(button->lastKey, Keys::Esc);
    EXPECT_EQ(owner->lastKey, Keys::Esc);
    FocusSystem::PopScope(dialog);
    FocusSystem::SetFocus(nullptr);
}

TEST_F(Focus, APointerPressTakesTheFocusAway)
{
    FakeWidget* a = Add("A", 0, 0);
    FocusSystem::SetFocus(a);
    FocusSystem::HandlePointerDown();
    EXPECT_EQ(FocusSystem::GetFocused(), nullptr);
    EXPECT_FALSE(a->hasFocus);
}

TEST_F(Focus, AWidgetThatGoesTakesItsFocusWithIt)
{
    FakeWidget* a = Add("A", 0, 0);
    FocusSystem::SetFocus(a);
    widgets.clear();
    EXPECT_EQ(FocusSystem::GetFocused(), nullptr);
}

TEST_F(Focus, DistanceRunsFromEdgeToEdge)
{
    // A wide slider over a row of two small switches, and a box centred far
    // below: Down goes to the row, which is nearer edge to edge, though the
    // box is the one straight below the centre.
    FakeWidget* slider = Add("Slider", 0, 3);
    slider->width = 12.0f;
    slider->height = 2.0f;
    FakeWidget* left = Add("Left", -2.5, 0.5);
    left->width = 3.0f;
    left->height = 1.6f;
    FakeWidget* right = Add("Right", 1.5, 0.5);
    right->width = 3.0f;
    right->height = 1.6f;
    Add("Box", 0, -2);
    FocusSystem::SetFocus(slider);
    FocusSystem::HandleKey(Keys::Down, 0);
    EXPECT_EQ(FocusSystem::GetFocused(), right);  // the row, and of it the nearer centre
    FocusSystem::SetFocus(nullptr);
}
