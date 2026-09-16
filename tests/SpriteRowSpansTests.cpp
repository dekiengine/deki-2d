// The opaque-run rule, pinned against the bug it was written for.
//
// A sprite row is split by the blitter into a straight copy over the opaque
// span and per-pixel blending either side. Getting the span wrong does not
// crash or assert: it draws the wrong pixels, in a finished frame, and that is
// how first-to-last-opaque survived until someone noticed hearts with black
// notches. Every case below is a row shape that distinguishes "longest opaque
// run" from a plausible wrong answer.

#include <gtest/gtest.h>

#include <SpriteRowSpans.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

// The package's types moved into its namespace; tests name them unqualified.
using namespace Deki2D;

namespace
{

constexpr uint8_t kOpaque = 255;
constexpr uint8_t kClear = 0;
constexpr uint8_t kSoft = 128;

// One row of RGB565A8 from a list of alpha values. Colour bytes are junk on
// purpose: the rule looks only at alpha, and a reader of the wrong byte picks
// these up.
std::vector<uint8_t> Row(std::initializer_list<uint8_t> alphas)
{
    std::vector<uint8_t> px;
    for (uint8_t a : alphas)
    {
        px.push_back(0xAB);
        px.push_back(0xCD);
        px.push_back(a);
    }
    return px;
}

struct Span
{
    int16_t start;
    int16_t end;
};

Span SpanOf(const std::vector<uint8_t>& row)
{
    const int32_t w = static_cast<int32_t>(row.size() / 3);
    int16_t spans[2] = {-1, -1};
    BuildOpaqueRowSpans(row.data(), w, 1, spans);
    return {spans[0], spans[1]};
}

}  // namespace

TEST(SpriteRowSpans, AFullyOpaqueRowIsOneSpanAcross)
{
    const Span s = SpanOf(Row({kOpaque, kOpaque, kOpaque, kOpaque}));
    EXPECT_EQ(s.start, 0);
    EXPECT_EQ(s.end, 4);
}

TEST(SpriteRowSpans, AHoleSplitsTheRowAndTheLongerSideWins)
{
    // The heart notch. First-to-last-opaque would answer [0,5) and copy the
    // transparent pixel at x=2 straight through.
    const Span s = SpanOf(Row({kOpaque, kOpaque, kClear, kOpaque, kOpaque, kOpaque}));
    EXPECT_EQ(s.start, 3) << "the span crosses the hole";
    EXPECT_EQ(s.end, 6);
}

TEST(SpriteRowSpans, ASoftPixelBreaksTheRunJustLikeATransparentOne)
{
    // Partial alpha has to blend. A run that swallows it copies it opaque.
    const Span s = SpanOf(Row({kOpaque, kOpaque, kOpaque, kSoft, kOpaque}));
    EXPECT_EQ(s.start, 0);
    EXPECT_EQ(s.end, 3);
}

TEST(SpriteRowSpans, TheLongestRunWinsWhateverItsPosition)
{
    // Leading run longer.
    Span s = SpanOf(Row({kOpaque, kOpaque, kOpaque, kClear, kOpaque}));
    EXPECT_EQ(s.start, 0);
    EXPECT_EQ(s.end, 3);

    // Trailing run longer.
    s = SpanOf(Row({kOpaque, kClear, kOpaque, kOpaque, kOpaque}));
    EXPECT_EQ(s.start, 2);
    EXPECT_EQ(s.end, 5);

    // Longest run in the middle.
    s = SpanOf(Row({kOpaque, kClear, kOpaque, kOpaque, kOpaque, kClear, kOpaque}));
    EXPECT_EQ(s.start, 2);
    EXPECT_EQ(s.end, 5);
}

TEST(SpriteRowSpans, TheFirstOfTwoEqualRunsWins)
{
    // Ties have to resolve the same way every build, or a sprite's spans
    // change under a compiler update.
    const Span s = SpanOf(Row({kOpaque, kOpaque, kClear, kOpaque, kOpaque}));
    EXPECT_EQ(s.start, 0);
    EXPECT_EQ(s.end, 2);
}

TEST(SpriteRowSpans, AnEmptyRowGetsAnEmptySpanAtTheRowEnd)
{
    // start = end = w, NOT start=w end=0. The latter made the blitter's left
    // and right regions both cover the whole row, blending every soft pixel
    // twice and starting the right loop at x=0 regardless of the clip rect.
    const Span s = SpanOf(Row({kClear, kClear, kClear}));
    EXPECT_EQ(s.start, 3);
    EXPECT_EQ(s.end, 3);
    EXPECT_EQ(s.end - s.start, 0) << "an empty span must have zero width";
}

TEST(SpriteRowSpans, ARowOfOnlySoftPixelsIsAlsoEmpty)
{
    const Span s = SpanOf(Row({kSoft, kSoft}));
    EXPECT_EQ(s.start, 2);
    EXPECT_EQ(s.end, 2);
}

TEST(SpriteRowSpans, ASingleOpaquePixelIsASpanOfOne)
{
    const Span s = SpanOf(Row({kClear, kOpaque, kClear}));
    EXPECT_EQ(s.start, 1);
    EXPECT_EQ(s.end, 2);
}

TEST(SpriteRowSpans, ARunTouchingTheRightEdgeIsClosedAtTheRowEnd)
{
    // The loop runs to x == w to close an open run. Without that the trailing
    // run is never recorded and the row reads as empty.
    const Span s = SpanOf(Row({kClear, kOpaque, kOpaque}));
    EXPECT_EQ(s.start, 1);
    EXPECT_EQ(s.end, 3);
}

TEST(SpriteRowSpans, EachRowIsComputedIndependently)
{
    // Three rows of four pixels: full, holed, empty. A row-stride mistake
    // shows up here and nowhere in the single-row cases above.
    std::vector<uint8_t> image;
    for (const auto& r : {Row({kOpaque, kOpaque, kOpaque, kOpaque}),
                          Row({kOpaque, kClear, kOpaque, kOpaque}),
                          Row({kClear, kClear, kClear, kClear})})
        image.insert(image.end(), r.begin(), r.end());

    int16_t spans[6] = {};
    BuildOpaqueRowSpans(image.data(), 4, 3, spans);

    EXPECT_EQ(spans[0], 0);
    EXPECT_EQ(spans[1], 4);
    EXPECT_EQ(spans[2], 2);
    EXPECT_EQ(spans[3], 4);
    EXPECT_EQ(spans[4], 4);
    EXPECT_EQ(spans[5], 4);
}

TEST(SpriteRowSpans, SpansNeverExceedTheRowWidth)
{
    // Whatever the row, the result has to be a usable [start, end) inside the
    // row: the blitter indexes with it directly.
    for (uint8_t a0 : {kClear, kSoft, kOpaque})
        for (uint8_t a1 : {kClear, kSoft, kOpaque})
            for (uint8_t a2 : {kClear, kSoft, kOpaque})
            {
                const Span s = SpanOf(Row({a0, a1, a2}));
                EXPECT_GE(s.start, 0);
                EXPECT_LE(s.end, 3);
                EXPECT_LE(s.start, s.end) << "start must not pass end";
            }
}
