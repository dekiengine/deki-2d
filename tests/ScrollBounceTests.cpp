// The rubber band a ScrollComponent with enableBounce shows at its ends: a
// drag past an end is slowed and limited, and the spring brings the list back
// at a speed that does not depend on the frame rate.

#include <gtest/gtest.h>

#include "ScrollBounce.h"

using namespace Deki2D::ScrollBounce;

namespace
{
constexpr float kMax = 10.0f;      // scroll range 0..10 m
constexpr float kViewport = 4.0f;  // half of it is the overscroll limit
}  // namespace

TEST(ScrollBounce, InsideTheRangeTheListFollowsTheFinger)
{
    EXPECT_FLOAT_EQ(Drag(5.0f, 2.0f, kMax, kViewport), 7.0f);
    EXPECT_FLOAT_EQ(Drag(5.0f, -3.0f, kMax, kViewport), 2.0f);
}

TEST(ScrollBounce, OnlyThePartPastAnEndIsSlowed)
{
    // 1 m to the start at full speed, then 1 m more at the drag resistance.
    EXPECT_FLOAT_EQ(Drag(1.0f, -2.0f, kMax, kViewport), -kDragResistance);
    EXPECT_FLOAT_EQ(Drag(9.0f, 2.0f, kMax, kViewport), kMax + kDragResistance);
    // Already past: the whole move is slowed.
    EXPECT_FLOAT_EQ(Drag(-0.5f, -1.0f, kMax, kViewport), -0.5f - kDragResistance);
}

TEST(ScrollBounce, MovingBackTowardTheRangeIsNotSlowed)
{
    EXPECT_FLOAT_EQ(Drag(-1.0f, 0.5f, kMax, kViewport), -0.5f);
    EXPECT_FLOAT_EQ(Drag(kMax + 1.0f, -0.5f, kMax, kViewport), kMax + 0.5f);
}

TEST(ScrollBounce, ADragStopsHalfAViewportPastTheEnd)
{
    EXPECT_FLOAT_EQ(Drag(0.0f, -100.0f, kMax, kViewport), -kViewport * kMaxOverscrollShare);
    EXPECT_FLOAT_EQ(Drag(kMax, 100.0f, kMax, kViewport), kMax + kViewport * kMaxOverscrollShare);
    // No viewport, no overscroll.
    EXPECT_FLOAT_EQ(Drag(0.0f, -5.0f, kMax, 0.0f), 0.0f);
}

TEST(ScrollBounce, TheSpringLeavesAListInsideItsRangeAlone)
{
    EXPECT_FLOAT_EQ(SpringBack(3.0f, kMax, 0.15f, 1.0f / 60.0f), 3.0f);
}

TEST(ScrollBounce, TheSpringClosesStiffnessOfTheGapPerFrame)
{
    // At 60 Hz one step closes exactly `stiffness` of the distance to the end.
    EXPECT_NEAR(SpringBack(-1.0f, kMax, 0.15f, 1.0f / 60.0f), -0.85f, 1e-5f);
    EXPECT_NEAR(SpringBack(kMax + 1.0f, kMax, 0.15f, 1.0f / 60.0f), kMax + 0.85f, 1e-5f);
}

TEST(ScrollBounce, TheSpringSpeedDoesNotDependOnTheFrameRate)
{
    float at60 = -1.0f;
    for (int i = 0; i < 60; ++i)
    {
        at60 = SpringBack(at60, kMax, 0.15f, 1.0f / 60.0f);
    }
    float at30 = -1.0f;
    for (int i = 0; i < 30; ++i)
    {
        at30 = SpringBack(at30, kMax, 0.15f, 1.0f / 30.0f);
    }
    EXPECT_NEAR(at60, at30, 1e-4f);
}

TEST(ScrollBounce, TheSpringArrivesExactlyAtTheEnd)
{
    float offset = -2.0f;
    int steps = 0;
    while (offset != 0.0f && steps < 1000)
    {
        offset = SpringBack(offset, kMax, 0.15f, 1.0f / 60.0f);
        ++steps;
    }
    EXPECT_EQ(offset, 0.0f);
    EXPECT_LT(steps, 120) << "a default spring settles within two seconds";
}
