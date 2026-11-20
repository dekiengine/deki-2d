// The PNG reader against PNG files made by Python's zlib (make_png_vectors.py):
// every colour type and bit depth, the three kinds of deflate block, every
// row filter, transparency, and picture data split over many chunks. Then
// shrinking, and broken files.

#include <gtest/gtest.h>

#include "PngDecoder.h"
#include "PngVectors.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace Deki2D;

namespace
{

// The picture read at the given limits, as RGBA rows top to bottom.
Png::Result Read(const uint8_t* data, size_t size, uint32_t maxW, uint32_t maxH, std::vector<uint8_t>& rgba,
                 uint32_t& w, uint32_t& h)
{
    rgba.clear();
    uint32_t nextRow = 0;
    bool inOrder = true;
    const Png::Result result = Png::Decode(
        data, size, maxW, maxH,
        [&](uint32_t y, const uint8_t* row, uint32_t width)
        {
            inOrder = inOrder && y == nextRow++;
            rgba.insert(rgba.end(), row, row + width * 4);
        },
        w, h);
    EXPECT_TRUE(inOrder) << "rows come once each, top to bottom";
    return result;
}

}  // namespace

TEST(PngDecoder, EveryKindReadsAsItsPixels)
{
    for (const auto& c : PngVectors::kCases)
    {
        std::vector<uint8_t> rgba;
        uint32_t w = 0;
        uint32_t h = 0;
        ASSERT_EQ(Read(c.png, c.size, 0, 0, rgba, w, h), Png::Result::Ok) << c.name;
        EXPECT_EQ(w, PngVectors::kWidth) << c.name;
        EXPECT_EQ(h, PngVectors::kHeight) << c.name;
        ASSERT_EQ(rgba.size(), size_t(PngVectors::kWidth) * PngVectors::kHeight * 4) << c.name;
        EXPECT_EQ(std::memcmp(rgba.data(), c.rgba, rgba.size()), 0) << c.name;
    }
}

TEST(PngDecoder, TheHeaderSaysTheSizeAndTransparency)
{
    for (const auto& c : PngVectors::kCases)
    {
        Png::Info info;
        ASSERT_EQ(Png::ReadInfo(c.png, c.size, info), Png::Result::Ok) << c.name;
        EXPECT_EQ(info.width, PngVectors::kWidth);
        EXPECT_EQ(info.height, PngVectors::kHeight);
        const std::string name = c.name;
        const bool see = name.rfind("rgba", 0) == 0 || name.rfind("greyalpha", 0) == 0 ||
                         name.rfind("palette", 0) == 0 || name.find("key") != std::string::npos;
        EXPECT_EQ(info.hasAlpha, see) << c.name;
    }
}

TEST(PngDecoder, FitSizeShrinksKeepingTheShapeAndNeverEnlarges)
{
    uint32_t w = 0;
    uint32_t h = 0;
    Png::FitSize(640, 480, 0, 0, w, h);
    EXPECT_EQ(w, 640u);
    EXPECT_EQ(h, 480u);
    Png::FitSize(640, 480, 1000, 1000, w, h);
    EXPECT_EQ(w, 640u);
    Png::FitSize(640, 480, 320, 240, w, h);
    EXPECT_EQ(w, 320u);
    EXPECT_EQ(h, 240u);
    Png::FitSize(640, 480, 320, 0, w, h);
    EXPECT_EQ(h, 240u);
    Png::FitSize(640, 480, 0, 120, w, h);
    EXPECT_EQ(w, 160u);
    Png::FitSize(4096, 16, 100, 100, w, h);
    EXPECT_EQ(w, 100u);
    EXPECT_EQ(h, 1u) << "never 0";
    Png::FitSize(300, 900, 200, 200, w, h);
    EXPECT_EQ(w, 67u);
    EXPECT_EQ(h, 200u);
}

TEST(PngDecoder, AShrunkPixelIsTheAlphaWeightedAverageOfWhatItCovers)
{
    for (const auto& c : PngVectors::kCases)
    {
        std::vector<uint8_t> full;
        std::vector<uint8_t> small;
        uint32_t fw = 0;
        uint32_t fh = 0;
        uint32_t sw = 0;
        uint32_t sh = 0;
        ASSERT_EQ(Read(c.png, c.size, 0, 0, full, fw, fh), Png::Result::Ok);
        ASSERT_EQ(Read(c.png, c.size, 6, 4, small, sw, sh), Png::Result::Ok) << c.name;
        uint32_t ew = 0;
        uint32_t eh = 0;
        Png::FitSize(fw, fh, 6, 4, ew, eh);
        ASSERT_EQ(sw, ew);
        ASSERT_EQ(sh, eh);
        ASSERT_EQ(small.size(), size_t(sw) * sh * 4);
        for (uint32_t oy = 0; oy < sh; ++oy)
        {
            for (uint32_t ox = 0; ox < sw; ++ox)
            {
                uint64_t sums[4] = {};
                uint64_t n = 0;
                for (uint32_t y = 0; y < fh; ++y)
                {
                    for (uint32_t x = 0; x < fw; ++x)
                    {
                        if (uint64_t(x) * sw / fw != ox || uint64_t(y) * sh / fh != oy)
                        {
                            continue;
                        }
                        const uint8_t* p = &full[(size_t(y) * fw + x) * 4];
                        for (int k = 0; k < 3; ++k)
                        {
                            sums[k] += uint64_t(p[k]) * p[3];
                        }
                        sums[3] += p[3];
                        ++n;
                    }
                }
                const uint8_t* got = &small[(size_t(oy) * sw + ox) * 4];
                const uint8_t alpha = sums[3] ? uint8_t((sums[3] + n / 2) / n) : 0;
                EXPECT_EQ(got[3], alpha) << c.name << " at " << ox << "," << oy;
                for (int k = 0; k < 3 && sums[3]; ++k)
                {
                    EXPECT_EQ(got[k], uint8_t((sums[k] + sums[3] / 2) / sums[3]))
                        << c.name << " at " << ox << "," << oy;
                }
            }
        }
    }
}

TEST(PngDecoder, WhatIsNotAPngOrIsNotReadSaysSo)
{
    const uint8_t text[] = "hello, this is not a picture";
    uint32_t w = 0;
    uint32_t h = 0;
    std::vector<uint8_t> rgba;
    EXPECT_EQ(Read(text, sizeof(text), 0, 0, rgba, w, h), Png::Result::NotPng);
    EXPECT_EQ(Read(nullptr, 0, 0, 0, rgba, w, h), Png::Result::NotPng);

    // The same file said to be interlaced, and said to be huge.
    std::vector<uint8_t> file(PngVectors::rgba8_dynamic_png,
                              PngVectors::rgba8_dynamic_png + sizeof(PngVectors::rgba8_dynamic_png));
    std::vector<uint8_t> interlaced = file;
    interlaced[8 + 8 + 12] = 1;
    EXPECT_EQ(Read(interlaced.data(), interlaced.size(), 0, 0, rgba, w, h), Png::Result::Unsupported);
    std::vector<uint8_t> huge = file;
    huge[8 + 8 + 0] = 0x7F;  // width 2^30 and more
    EXPECT_EQ(Read(huge.data(), huge.size(), 0, 0, rgba, w, h), Png::Result::TooLarge);

    // Cut short anywhere: never Ok, never a crash.
    for (size_t cut = 0; cut < file.size() - 12; cut += 3)
    {
        EXPECT_NE(Read(file.data(), cut, 0, 0, rgba, w, h), Png::Result::Ok) << "cut at " << cut;
    }
}

TEST(PngDecoder, BrokenBytesNeverCrashIt)
{
    // Each file with bytes changed at random, many times: whatever comes back,
    // the reader stays inside the file and its own buffers (run it under
    // AddressSanitizer to see that, not only that it returns).
    std::mt19937 random(7);
    for (const auto& c : PngVectors::kCases)
    {
        for (int round = 0; round < 60; ++round)
        {
            std::vector<uint8_t> file(c.png, c.png + c.size);
            const int changes = 1 + int(random() % 4);
            for (int i = 0; i < changes; ++i)
            {
                // Past the signature, so most rounds reach the inflater.
                file[8 + random() % (file.size() - 8)] = uint8_t(random());
            }
            std::vector<uint8_t> rgba;
            uint32_t w = 0;
            uint32_t h = 0;
            const Png::Result result = Read(file.data(), file.size(), 0, 0, rgba, w, h);
            if (result == Png::Result::Ok)
            {
                EXPECT_EQ(rgba.size(), size_t(w) * h * 4);
            }
        }
    }
}
