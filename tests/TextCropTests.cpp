// The crop pivot.
//
// A text bake is cropped to the columns and rows that hold a pixel, and the
// crop's pivot keeps it where the whole bake was. Wrong, the text moves by a
// pixel; the loop that finds it once never ended for a short line at the
// start of a wide box, and the game hung on that text.

#include <gtest/gtest.h>

#include <TextCrop.h>

#include <cmath>
#include <cstdint>

// The package's types are in its namespace; tests name them unqualified.
using namespace Deki2D;

namespace
{

// Where Blit puts the crop's first pixel, relative to the anchor, against
// where the whole bake put that same pixel.
void ExpectSamePlace(int32_t full, int32_t first, int32_t crop)
{
    const float pivot = CropPivot(full, first, crop);
    const int32_t cropStart = -static_cast<int32_t>(std::floor(static_cast<float>(crop) * pivot));
    const int32_t wholeStart = -(full / 2) + first;
    EXPECT_EQ(cropStart, wholeStart) << "full " << full << " first " << first << " crop " << crop;
}

}  // namespace

TEST(TextCrop, WholeBakeKeepsTheCentre)
{
    EXPECT_FLOAT_EQ(CropPivot(40, 0, 40), 0.5f);
    ExpectSamePlace(41, 0, 41);
}

// A short line at the start of a wide box: the pivot is above 2. These were
// the first widths that never returned.
TEST(TextCrop, ShortLineInAWideBoxReturns)
{
    ExpectSamePlace(52, 0, 11);
    ExpectSamePlace(54, 1, 11);
    ExpectSamePlace(62, 0, 7);
}

// A short line at the end of a wide box: the pivot is negative.
TEST(TextCrop, LineAtTheEndOfTheBox)
{
    ExpectSamePlace(60, 50, 10);
    ExpectSamePlace(61, 58, 3);
}

TEST(TextCrop, EveryCropOfSmallBoxesKeepsItsPlace)
{
    for (int32_t full = 1; full <= 120; full++)
    {
        for (int32_t first = 0; first < full; first++)
        {
            for (int32_t crop = 1; first + crop <= full; crop++)
            {
                ExpectSamePlace(full, first, crop);
            }
        }
    }
}
