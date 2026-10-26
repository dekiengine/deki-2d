#include "GradientComponent.h"
#include "PixelFormat.h"
#include <cstring>
#include <deki/Object.h>
#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include "deki-rendering/CameraComponent.h"
#include "deki-rendering/QuadBlit.h"
// In every build: the bake is allocated through Deki::Memory, so the editor
// takes the same failure path as the device.
#include <deki/providers/Memory.h>
#include <cmath>
#include <algorithm>

namespace Deki2D
{

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// How far from 0 an angle's cos or sin may be and still count as 0: float
// pi/2 and pi are off by under 1e-7.
constexpr float kStraightEpsilon = 1e-6f;

// Bayer matrices for ordered dithering
static const uint8_t kBayer2x2[4] = { 0, 2, 3, 1 };

static const uint8_t kBayer4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

static const uint8_t kBayer8x8[64] = { 0,  32, 8,  40, 2,  34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
                                       12, 44, 4,  36, 14, 46, 6,  38, 60, 28, 52, 20, 62, 30, 54, 22,
                                       3,  35, 11, 43, 1,  33, 9,  41, 51, 19, 59, 27, 49, 17, 57, 25,
                                       15, 47, 7,  39, 13, 45, 5,  37, 63, 31, 55, 23, 61, 29, 53, 21 };

// 16x16: 256 values, 0-255
static const uint8_t kBayer16x16[256] = {
    0,   192, 48,  240, 12,  204, 60,  252, 3,   195, 51,  243, 15,  207, 63,  255, 128, 64,  176, 112, 140, 76,
    188, 124, 131, 67,  179, 115, 143, 79,  191, 127, 32,  224, 16,  208, 44,  236, 28,  220, 35,  227, 19,  211,
    47,  239, 31,  223, 160, 96,  144, 80,  172, 108, 156, 92,  163, 99,  147, 83,  175, 111, 159, 95,  8,   200,
    56,  248, 4,   196, 52,  244, 11,  203, 59,  251, 7,   199, 55,  247, 136, 72,  184, 120, 132, 68,  180, 116,
    139, 75,  187, 123, 135, 71,  183, 119, 40,  232, 24,  216, 36,  228, 20,  212, 43,  235, 27,  219, 39,  231,
    23,  215, 168, 104, 152, 88,  164, 100, 148, 84,  171, 107, 155, 91,  167, 103, 151, 87,  2,   194, 50,  242,
    14,  206, 62,  254, 1,   193, 49,  241, 13,  205, 61,  253, 130, 66,  178, 114, 142, 78,  190, 126, 129, 65,
    177, 113, 141, 77,  189, 125, 34,  226, 18,  210, 46,  238, 30,  222, 33,  225, 17,  209, 45,  237, 29,  221,
    162, 98,  146, 82,  174, 110, 158, 94,  161, 97,  145, 81,  173, 109, 157, 93,  10,  202, 58,  250, 6,   198,
    54,  246, 9,   201, 57,  249, 5,   197, 53,  245, 138, 74,  186, 122, 134, 70,  182, 118, 137, 73,  185, 121,
    133, 69,  181, 117, 42,  234, 26,  218, 38,  230, 22,  214, 41,  233, 25,  217, 37,  229, 21,  213, 170, 106,
    154, 90,  166, 102, 150, 86,  169, 105, 153, 89,  165, 101, 149, 85
};

GradientComponent::GradientComponent(float w, float h)
    : DekiRendering::RendererComponent(),
      gradientType(GradientType::Linear),
      tileMode(GradientTileMode::None),
      ditherMode(GradientDitherMode::Ordered4x4),
      ditherScale(1),
      width(w),
      height(h),
      angle(0.0f),
      centerX(0.5f),
      centerY(0.5f),
      radius(0.5f),
      stopCount(2),
      stop1Position(0.0f),
      stop1Color(Deki::Color::White),
      stop2Position(1.0f),
      stop2Color(Deki::Color::Black),
      stop3Position(0.0f),
      stop3Color(Deki::Color::Black),
      stop4Position(0.0f),
      stop4Color(Deki::Color::Black),
      tileWidth(0.0f),
      tileHeight(0.0f)
{
    // White to black by default
    stops[0] = GradientStop(0.0f, Deki::Color::White);
    stops[1] = GradientStop(1.0f, Deki::Color::Black);
    stops[2] = GradientStop();
    stops[3] = GradientStop();
}

GradientComponent::~GradientComponent()
{
    // Copies QuadBlit scaled from the bake (cacheScaled) go with it, so a
    // new buffer at the same address is never mistaken for this one.
    ForgetBake();
}

void GradientComponent::ForgetBake()
{
    if (m_Baked)
    {
        QuadBlit::ForgetScaled(m_Baked.Data(), m_Baked.Bytes());
    }
}

void GradientComponent::WriteStopsToProperties()
{
    if (stopCount >= 1)
    {
        stop1Position = stops[0].position;
        stop1Color = stops[0].color;
    }
    if (stopCount >= 2)
    {
        stop2Position = stops[1].position;
        stop2Color = stops[1].color;
    }
    if (stopCount >= 3)
    {
        stop3Position = stops[2].position;
        stop3Color = stops[2].color;
    }
    if (stopCount >= 4)
    {
        stop4Position = stops[3].position;
        stop4Color = stops[3].color;
    }
}

uint64_t GradientComponent::ComputeBakeKey(int32_t widthPx, int32_t heightPx) const
{
    // FNV-1a over a byte snapshot of everything RenderToBuffer reads.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* data, size_t size)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            h = (h ^ p[i]) * 1099511628211ull;
        }
    };
    auto mixf = [&mix](float f)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        mix(&bits, sizeof(bits));
    };
    const uint8_t type = static_cast<uint8_t>(gradientType);
    const uint8_t tile = static_cast<uint8_t>(tileMode);
    const uint8_t dither = static_cast<uint8_t>(ditherMode);
    mix(&type, 1);
    mix(&tile, 1);
    mix(&dither, 1);
    mix(&ditherScale, 1);
    mix(&widthPx, sizeof(widthPx));
    mix(&heightPx, sizeof(heightPx));
    mixf(angle);
    mixf(centerX);
    mixf(centerY);
    mixf(radius);
    mixf(tileWidth);
    mixf(tileHeight);
    mix(&stopCount, 1);
    for (int i = 0; i < stopCount && i < kMaxStops; ++i)
    {
        mixf(stops[i].position);
        mix(&stops[i].color, sizeof(stops[i].color));
    }
    return h;
}

void GradientComponent::SetGradientType(GradientType type)
{
    gradientType = type;
}

void GradientComponent::SetLinearGradient(float angleRadians)
{
    gradientType = GradientType::Linear;
    angle = angleRadians;  // Radians, the engine convention
}

void GradientComponent::SetRadialGradient(float centerXPos, float centerYPos, float radiusVal)
{
    gradientType = GradientType::Radial;
    centerX = std::clamp(centerXPos, 0.0f, 1.0f);
    centerY = std::clamp(centerYPos, 0.0f, 1.0f);
    radius = std::clamp(radiusVal, 0.0f, 1.0f);
}

void GradientComponent::AddColorStop(float position, uint8_t r, uint8_t g, uint8_t b)
{
    if (stopCount >= kMaxStops)
    {
        return;
    }

    stops[stopCount] = GradientStop(std::clamp(position, 0.0f, 1.0f), r, g, b);
    stopCount++;

    // Keep stops sorted by position (bubble sort: at most 4 stops)
    for (int i = 0; i < stopCount - 1; i++)
    {
        for (int j = 0; j < stopCount - i - 1; j++)
        {
            if (stops[j].position > stops[j + 1].position)
            {
                GradientStop temp = stops[j];
                stops[j] = stops[j + 1];
                stops[j + 1] = temp;
            }
        }
    }
    WriteStopsToProperties();
}

void GradientComponent::ClearColorStops()
{
    stopCount = 0;
}

void GradientComponent::SyncStopsFromProperties()
{
    if (stopCount >= 1)
    {
        stops[0].position = stop1Position;
        stops[0].color = stop1Color;
    }
    if (stopCount >= 2)
    {
        stops[1].position = stop2Position;
        stops[1].color = stop2Color;
    }
    if (stopCount >= 3)
    {
        stops[2].position = stop3Position;
        stops[2].color = stop3Color;
    }
    if (stopCount >= 4)
    {
        stops[3].position = stop4Position;
        stops[3].color = stop4Color;
    }
}

void GradientComponent::SetSimpleGradient(uint8_t startR, uint8_t startG, uint8_t startB, uint8_t endR, uint8_t endG,
                                          uint8_t endB)
{
    ClearColorStops();
    AddColorStop(0.0f, startR, startG, startB);
    AddColorStop(1.0f, endR, endG, endB);
}

void GradientComponent::SetTiling(GradientTileMode mode, float tileW, float tileH)
{
    tileMode = mode;
    tileWidth = tileW;
    tileHeight = tileH;
}

void GradientComponent::SetDithering(GradientDitherMode mode)
{
    ditherMode = mode;
}

void GradientComponent::SetArea(float w, float h)
{
    width = w;
    height = h;
}

DEKI_FAST_ATTR float GradientComponent::CalculateGradientPosition(float normX, float normY) const
{
    switch (gradientType)
    {
        case GradientType::Linear:
        {
            // The angle's cos/sin and the projection's range over the unit
            // square are worked out once per bake in RenderToBuffer.
            const float projection = normX * m_CosAngle + normY * m_SinAngle;
            const float along = projection - m_LinearLow;
            // A range that is a power of two (any gradient along an axis)
            // scales exactly by its inverse; division is done in software on
            // an ESP32 and was most of a bake.
            return std::clamp(m_LinearInverse > 0.0f ? along * m_LinearInverse : along / m_LinearRange, 0.0f, 1.0f);
        }

        case GradientType::Radial:
        {
            float dx = normX - centerX;
            float dy = normY - centerY;
            float distance = sqrtf(dx * dx + dy * dy);
            return std::clamp(distance / radius, 0.0f, 1.0f);
        }

        case GradientType::Conical:
        {
            float dx = normX - centerX;
            float dy = normY - centerY;
            float angle_rad = atan2f(dy, dx) + M_PI;  // 0 to 2 pi
            return angle_rad / (2.0f * M_PI);
        }

        default: return normX;
    }
}

DEKI_FAST_ATTR void GradientComponent::InterpolateColor(float position, uint8_t* r, uint8_t* g, uint8_t* b) const
{
    if (stopCount == 0)
    {
        *r = *g = *b = 0;
        return;
    }

    if (stopCount == 1)
    {
        *r = stops[0].color.r;
        *g = stops[0].color.g;
        *b = stops[0].color.b;
        return;
    }

    position = std::clamp(position, 0.0f, 1.0f);

    // Before the first stop or past the last, the end colour holds
    if (position <= stops[0].position)
    {
        *r = stops[0].color.r;
        *g = stops[0].color.g;
        *b = stops[0].color.b;
        return;
    }

    if (position >= stops[stopCount - 1].position)
    {
        *r = stops[stopCount - 1].color.r;
        *g = stops[stopCount - 1].color.g;
        *b = stops[stopCount - 1].color.b;
        return;
    }

    for (int i = 0; i < stopCount - 1; i++)
    {
        if (position >= stops[i].position && position <= stops[i + 1].position)
        {
            float range = stops[i + 1].position - stops[i].position;
            float t = (position - stops[i].position) / range;

            *r = (uint8_t)(stops[i].color.r + t * (stops[i + 1].color.r - stops[i].color.r));
            *g = (uint8_t)(stops[i].color.g + t * (stops[i + 1].color.g - stops[i].color.g));
            *b = (uint8_t)(stops[i].color.b + t * (stops[i + 1].color.b - stops[i].color.b));
            return;
        }
    }

    *r = *g = *b = 0;
}

DEKI_FAST_ATTR float GradientComponent::SampleBayerThreshold(int32_t x, int32_t y) const
{
    // M / N^2, as Pixelorama does (no +0.5 centring), which gives crisp
    // hard-edged transitions at t=0 and t=1.
    switch (ditherMode)
    {
        case GradientDitherMode::Ordered2x2: return kBayer2x2[(y & 1) * 2 + (x & 1)] / 4.0f;
        case GradientDitherMode::Ordered4x4: return kBayer4x4[(y & 3) * 4 + (x & 3)] / 16.0f;
        case GradientDitherMode::Ordered8x8: return kBayer8x8[(y & 7) * 8 + (x & 7)] / 64.0f;
        case GradientDitherMode::Ordered16x16: return kBayer16x16[(y & 15) * 16 + (x & 15)] / 256.0f;
        default: return 0.0f;
    }
}

DEKI_FAST_ATTR void GradientComponent::PickStopByThreshold(float position, float threshold, uint8_t* r, uint8_t* g,
                                                           uint8_t* b) const
{
    // Pixelorama-style dithering: rather than blending, pick one of the two
    // stops around the position, by whether the position between them
    // reaches a Bayer threshold. Every pixel is exactly one of the authored
    // colours, and the pattern fills the transitions: the stippled pixel-art
    // look.
    //
    // Matches Pixelorama's Gradient.gdshader:
    //   - position < stops[0].position: first stop, solid
    //   - position >= stops[N-1].position: last stop, solid
    //   - between two stops: local_t >= threshold picks the upper stop
    if (stopCount == 0)
    {
        *r = *g = *b = 0;
        return;
    }
    if (stopCount == 1)
    {
        *r = stops[0].color.r;
        *g = stops[0].color.g;
        *b = stops[0].color.b;
        return;
    }

    if (position < stops[0].position)
    {
        *r = stops[0].color.r;
        *g = stops[0].color.g;
        *b = stops[0].color.b;
        return;
    }
    if (position >= stops[stopCount - 1].position)
    {
        *r = stops[stopCount - 1].color.r;
        *g = stops[stopCount - 1].color.g;
        *b = stops[stopCount - 1].color.b;
        return;
    }

    for (int i = 0; i < stopCount - 1; i++)
    {
        if (position >= stops[i].position && position < stops[i + 1].position)
        {
            float range = stops[i + 1].position - stops[i].position;
            float local_t = (range > 0.0f) ? (position - stops[i].position) / range : 0.0f;

            const GradientStop& picked = (local_t >= threshold) ? stops[i + 1] : stops[i];
            *r = picked.color.r;
            *g = picked.color.g;
            *b = picked.color.b;
            return;
        }
    }

    *r = *g = *b = 0;
}

DEKI_FAST_ATTR uint16_t GradientComponent::ConvertToRGB565(uint8_t r, uint8_t g, uint8_t b) const
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void GradientComponent::RenderPixel(int32_t x, int32_t y, uint16_t color, uint8_t* renderBuffer, int screenWidth,
                                    int screenHeight) const
{
    if (x < 0 || x >= screenWidth || y < 0 || y >= screenHeight)
    {
        return;
    }

    uint16_t* buffer16 = (uint16_t*)renderBuffer;
    buffer16[y * screenWidth + x] = color;
}

void GradientComponent::PrepareBake()
{
    // The inspector edits the stopN properties
    SyncStopsFromProperties();
    m_CosAngle = cosf(angle);
    m_SinAngle = sinf(angle);
    // Straight along an axis when the angle is a whole number of right
    // angles: cos(pi/2) is -4.4e-8 in float, which tilted a vertical gradient
    // by a hair, and where a row met a colour's edge one pixel of it turned.
    if (std::fabs(m_CosAngle) < kStraightEpsilon)
    {
        m_CosAngle = 0.0f;
        m_SinAngle = m_SinAngle < 0.0f ? -1.0f : 1.0f;
    }
    else if (std::fabs(m_SinAngle) < kStraightEpsilon)
    {
        m_SinAngle = 0.0f;
        m_CosAngle = m_CosAngle < 0.0f ? -1.0f : 1.0f;
    }
    // Over the unit square a linear gradient's projection runs from
    // m_LinearLow to m_LinearLow + m_LinearRange.
    m_LinearLow = std::min(0.0f, m_CosAngle) + std::min(0.0f, m_SinAngle);
    m_LinearRange = (std::max(0.0f, m_CosAngle) + std::max(0.0f, m_SinAngle)) - m_LinearLow;
    int exponent = 0;
    m_LinearInverse = std::frexp(m_LinearRange, &exponent) == 0.5f ? 1.0f / m_LinearRange : 0.0f;
}

void GradientComponent::TileSizes(int32_t artW, int32_t artH, int32_t& tileW, int32_t& tileH) const
{
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t tileWPx = static_cast<int32_t>(tileWidth * ppm);
    const int32_t tileHPx = static_cast<int32_t>(tileHeight * ppm);
    tileW = tileWPx > 0 ? tileWPx : artW;
    tileH = tileHPx > 0 ? tileHPx : artH;
}

float GradientComponent::NormAlong(int32_t i, int32_t full, int32_t tile, bool tiled) const
{
    if (tileMode == GradientTileMode::Mirror)
    {
        const int32_t t = i % (tile * 2);
        return t < tile ? (float)t / tile : 1.0f - (float)(t - tile) / tile;
    }
    return tiled ? (float)(i % tile) / tile : (float)i / full;
}

void GradientComponent::FindStraightAxes(int32_t artW, int32_t artH, bool& sameAcross, bool& sameDown)
{
    sameAcross = false;
    sameDown = false;
    // A dither's pattern changes along both axes, and a radial or conical
    // gradient's position does.
    if (stopCount == 0 || ditherMode != GradientDitherMode::None || gradientType != GradientType::Linear || artW <= 0 ||
        artH <= 0)
    {
        return;
    }
    PrepareBake();
    int32_t tileW = 0;
    int32_t tileH = 0;
    TileSizes(artW, artH, tileW, tileH);
    const bool mirror = tileMode == GradientTileMode::Mirror;
    const bool tileAcross = tileMode == GradientTileMode::Horizontal || tileMode == GradientTileMode::Both;
    const bool tileDown = tileMode == GradientTileMode::Vertical || tileMode == GradientTileMode::Both;
    // The spans the position moves one way along (see RenderToBuffer): the
    // whole width, or a tile, or half a mirrored one.
    const int32_t spanX = (tileAcross || mirror) ? tileW : artW;
    const int32_t spanY = (tileDown || mirror) ? tileH : artH;

    // One colour along a line: the position only moves one way within a span,
    // so every position on the line lies between the spans' ends. Between two
    // stops each channel only moves one way with it too, so when the ends are
    // the same colour and no stop lies inside their range, every pixel
    // between is that colour. (Compared by colour, not position: cos(pi/2) is
    // not 0 in float, so a vertical gradient's positions differ along a row
    // by a hair that no pixel shows.)
    auto pixelAt = [this](float position) -> uint16_t
    {
        uint8_t r, g, b;
        InterpolateColor(position, &r, &g, &b);
        return ConvertToRGB565(r, g, b);
    };
    auto sameAlong = [this, &pixelAt](int32_t count, int32_t span, auto&& at) -> bool
    {
        const float first = at(0);
        const uint16_t colour = pixelAt(first);
        float low = first;
        float high = first;
        for (int32_t start = 0; start < count; start += span)
        {
            const int32_t last = std::min(start + span, count) - 1;
            for (const float p : { at(start), at(last) })
            {
                if (p != p || pixelAt(p) != colour)
                {
                    return false;
                }
                low = std::min(low, p);
                high = std::max(high, p);
            }
        }
        for (int i = 0; i < stopCount; i++)
        {
            if (stops[i].position > low && stops[i].position < high)
            {
                return false;
            }
        }
        return true;
    };

    sameAcross = true;
    for (int32_t y = 0; y < artH && sameAcross; y++)
    {
        const float normY = NormAlong(y, artH, tileH, tileDown);
        sameAcross = sameAlong(artW, spanX, [&](int32_t x)
                               { return CalculateGradientPosition(NormAlong(x, artW, tileW, tileAcross), normY); });
    }
    sameDown = true;
    for (int32_t x = 0; x < artW && sameDown; x++)
    {
        const float normX = NormAlong(x, artW, tileW, tileAcross);
        sameDown = sameAlong(artH, spanY, [&](int32_t y)
                             { return CalculateGradientPosition(normX, NormAlong(y, artH, tileH, tileDown)); });
    }
}

bool GradientComponent::RenderToBuffer(uint8_t* buffer, int32_t outW, int32_t outH, int32_t artW, int32_t artH,
                                       int32_t ditherArt)
{
    if (!buffer || stopCount == 0)
    {
        return false;
    }

    // width/height are world meters; the layout runs on the art grid.
    const int32_t widthPx = artW;
    const int32_t heightPx = artH;
    if (widthPx <= 0 || heightPx <= 0 || outW <= 0 || outH <= 0)
    {
        return false;
    }
    if (ditherArt < 1)
    {
        ditherArt = 1;
    }

    PrepareBake();

    uint16_t* buffer16 = reinterpret_cast<uint16_t*>(buffer);
    int32_t renderWidth = widthPx;
    int32_t renderHeight = heightPx;

    int32_t actualTileWidth = 0;
    int32_t actualTileHeight = 0;
    TileSizes(renderWidth, renderHeight, actualTileWidth, actualTileHeight);

    // The Bayer pattern is laid out on the art grid, cells ditherArt art
    // pixels wide, unless the bake is smaller than the art: then it is on the
    // output grid, cells ditherArt art pixels in whole output pixels, so it
    // stays a regular stipple instead of art pixels dropped unevenly.
    const bool ditherOnArt = outW >= widthPx && outH >= heightPx;
    const int32_t outCell =
        std::max<int32_t>(1, static_cast<int32_t>(std::lround(static_cast<double>(ditherArt) * outW / widthPx)));

    const bool tileAcross = tileMode == GradientTileMode::Horizontal || tileMode == GradientTileMode::Both;
    const bool tileDown = tileMode == GradientTileMode::Vertical || tileMode == GradientTileMode::Both;

    // Per art column: its place across (worked out once, not on every row),
    // then the current art row's gradient position, and its pixel when the
    // gradient is not dithered.
    Deki::Buffer<uint8_t> table;
    const size_t tableColumns = static_cast<size_t>(renderWidth);
    if (!table.Allocate(tableColumns * (2 * sizeof(float) + sizeof(uint16_t)), Deki::Memory::External))
    {
        DEKI_LOG_WARNING("GradientComponent: no room for the %d columns of a bake; not drawing it", (int)renderWidth);
        return false;
    }
    float* columns = reinterpret_cast<float*>(table.Data());
    float* positions = columns + tableColumns;
    uint16_t* artPixels = reinterpret_cast<uint16_t*>(positions + tableColumns);
    for (int32_t x = 0; x < renderWidth; x++)
    {
        columns[x] = NormAlong(x, renderWidth, actualTileWidth, tileAcross);
    }

    // Along a row, a linear gradient's position only ever moves one way while
    // the columns do: across the whole width, or one tile (half a mirrored
    // one) at a time. Two columns at the same position then have every column
    // between them at it too, so runs of one position are found by galloping
    // out from their first column and filled, instead of worked out column by
    // column. A gradient along an axis is a run or two a row.
    const bool monotonic = gradientType == GradientType::Linear;
    int32_t span = renderWidth;
    if (tileAcross || tileMode == GradientTileMode::Mirror)
    {
        span = actualTileWidth;
    }
    const bool dithered = ditherMode != GradientDitherMode::None;

    // Neighbouring pixels mostly land on the same colour, so the last one is
    // kept. The bake used to work every pixel out from scratch, and a
    // full-screen background took 0.7 s on an ESP32-S3, the first frame of
    // every scene that has one.
    bool haveLast = false;
    float lastPos = 0.0f;
    float lastThreshold = 0.0f;
    uint16_t lastPixel = 0;
    auto pixelAt = [&](float gradPos, float threshold) -> uint16_t
    {
        if (!haveLast || gradPos != lastPos || threshold != lastThreshold)
        {
            uint8_t r, g, b;
            if (!dithered)
            {
                // No dither: blend smoothly between stops
                InterpolateColor(gradPos, &r, &g, &b);
            }
            else
            {
                PickStopByThreshold(gradPos, threshold, &r, &g, &b);
            }
            lastPixel = DekiPixel::ToMemory565(ConvertToRGB565(r, g, b));
            lastPos = gradPos;
            lastThreshold = threshold;
            haveLast = true;
        }
        return lastPixel;
    };

    // Art columns first to last at one position. Undithered, that is one
    // pixel for all of them.
    auto fill = [&](int32_t first, int32_t last, float gradPos)
    {
        if (dithered)
        {
            std::fill(positions + first, positions + last + 1, gradPos);
        }
        else
        {
            std::fill(artPixels + first, artPixels + last + 1, pixelAt(gradPos, 0.0f));
        }
    };

    for (int32_t oy = 0; oy < outH; oy++)
    {
        // The art row this output row covers (the same row for every output
        // row of a whole-number upscale).
        const int32_t y = static_cast<int32_t>((static_cast<int64_t>(oy) * renderHeight) / outH);
        const int32_t cellY = ditherOnArt ? y / ditherArt : oy / outCell;
        uint16_t* row = buffer16 + static_cast<size_t>(oy) * outW;

        // An output row on the same art row as the one above has its columns.
        if (oy == 0 || y != static_cast<int32_t>((static_cast<int64_t>(oy - 1) * renderHeight) / outH))
        {
            const float normY = NormAlong(y, renderHeight, actualTileHeight, tileDown);
            if (!monotonic)
            {
                for (int32_t x = 0; x < renderWidth; x++)
                {
                    fill(x, x, CalculateGradientPosition(columns[x], normY));
                }
            }
            for (int32_t start = 0; monotonic && start < renderWidth; start += span)
            {
                const int32_t end = std::min(start + span, renderWidth);
                int32_t x = start;
                float at = CalculateGradientPosition(columns[x], normY);
                while (x < end)
                {
                    // The run reaches `last` and stops before `beyond`, where
                    // the position is `beyondAt` (unless `beyond` is the end).
                    int32_t last = x;
                    int32_t beyond = end;
                    float beyondAt = 0.0f;
                    for (int32_t step = 1; last + step < end; step *= 2)
                    {
                        const float p = CalculateGradientPosition(columns[last + step], normY);
                        if (p != at)
                        {
                            beyond = last + step;
                            beyondAt = p;
                            break;
                        }
                        last += step;
                    }
                    while (beyond - last > 1)
                    {
                        const int32_t mid = last + (beyond - last) / 2;
                        const float p = CalculateGradientPosition(columns[mid], normY);
                        if (p == at)
                        {
                            last = mid;
                        }
                        else
                        {
                            beyond = mid;
                            beyondAt = p;
                        }
                    }
                    fill(x, last, at);
                    x = beyond;
                    at = beyondAt;
                }
            }
        }

        if (!dithered && outW == renderWidth)
        {
            std::memcpy(row, artPixels, static_cast<size_t>(outW) * sizeof(uint16_t));
            continue;
        }

        // The art column under output column ox is ox * renderWidth / outW,
        // stepped along with its remainder instead of divided out per pixel.
        int32_t x = 0;
        int32_t xRemainder = 0;
        for (int32_t ox = 0; ox < outW; ox++)
        {
            if (dithered)
            {
                // Stipple dither picks one of the two stops around the
                // position by a Bayer threshold, read on the art grid like the
                // colour (see RenderToBuffer in the header): zooming magnifies
                // the device's pattern instead of laying out a new one.
                const int32_t cellX = ditherOnArt ? x / ditherArt : ox / outCell;
                row[ox] = pixelAt(positions[x], SampleBayerThreshold(cellX, cellY));
            }
            else
            {
                row[ox] = artPixels[x];
            }

            xRemainder += renderWidth;
            if (xRemainder >= outW)
            {
                x += xRemainder / outW;
                xRemainder %= outW;
            }
        }
    }
    return true;
}

void GradientComponent::CheckBakeUniformity(int32_t widthPx, int32_t heightPx)
{
    // Read off the pixels rather than the angle: cos(pi/2) is not exactly 0
    // in float, so a "vertical" gradient could still differ along a row, and
    // a dithered one does. Each check stops at the first difference.
    const uint16_t* px = reinterpret_cast<const uint16_t*>(m_Baked.Data());
    const size_t w = static_cast<size_t>(widthPx);
    m_UniformRows = true;
    for (int32_t y = 0; y < heightPx && m_UniformRows; ++y)
    {
        const uint16_t* row = px + static_cast<size_t>(y) * w;
        for (size_t x = 1; x < w; ++x)
        {
            if (row[x] != row[0])
            {
                m_UniformRows = false;
                break;
            }
        }
    }
    m_UniformColumns = !m_UniformRows;
    for (int32_t y = 1; y < heightPx && m_UniformColumns; ++y)
    {
        m_UniformColumns = std::memcmp(px + static_cast<size_t>(y) * w, px, w * sizeof(uint16_t)) == 0;
    }
    // A one-column bake is its own row colours.
    if (m_UniformRows && widthPx > 1 && m_RowColors.Allocate(static_cast<size_t>(heightPx), Deki::Memory::Internal))
    {
        for (int32_t y = 0; y < heightPx; ++y)
        {
            m_RowColors[y] = px[static_cast<size_t>(y) * w];
        }
    }
    else
    {
        m_RowColors.Reset();
    }
}

bool GradientComponent::RenderContent(const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX,
                                      float& outPivotY, uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB,
                                      uint8_t& outTintA)
{
    if (!owner || stopCount == 0)
    {
        return false;
    }

    // width/height are world meters. The layout is on the art grid (the
    // project's pixels per meter); see RenderToBuffer.
    const float artPPM = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t artW = static_cast<int32_t>(width * artPPM);
    const int32_t artH = static_cast<int32_t>(height * artPPM);
    if (artW <= 0 || artH <= 0)
    {
        return false;
    }

    // Drawn at its own size or larger, it is baked on the art grid: every art
    // pixel is one colour, so a bigger bake would only repeat pixels, and
    // QuadBlit magnifies it evenly (each art pixel the same whole number of
    // screen pixels, give or take one) without a re-bake on every zoom.
    // Drawn smaller (a camera showing more art than the screen has pixels, the
    // editor zoomed out), it is baked at the view's density, 1:1 on the
    // screen, so no art pixel is dropped unevenly as it is drawn.
    int32_t widthPx = artW;
    int32_t heightPx = artH;
    const DekiRendering::DrawView& view = DekiRendering::CurrentDrawView();
    if (view.pixelsPerMeter > 0.0f && view.pixelsPerMeter < artPPM)
    {
        float bakePPM = view.pixelsPerMeter;
        // At most twice the view's own area: a big gradient mostly off the
        // view would otherwise ask for a bake many screens wide. Past that it
        // is scaled up a little as it is drawn.
        const double viewArea = static_cast<double>(view.width) * view.height;
        const double bakeArea = static_cast<double>(width) * bakePPM * height * bakePPM;
        if (viewArea > 0.0 && bakeArea > viewArea * 2.0)
        {
            bakePPM *= static_cast<float>(std::sqrt(viewArea * 2.0 / bakeArea));
        }
        widthPx = std::max<int32_t>(1, static_cast<int32_t>(std::lround(width * bakePPM)));
        heightPx = std::max<int32_t>(1, static_cast<int32_t>(std::lround(height * bakePPM)));
    }

    // A Bayer cell covers ditherScale art pixels (a power of two, 1..16).
    int32_t ditherArt = 1;
    if (ditherScale >= 16)
    {
        ditherArt = 16;
    }
    else if (ditherScale >= 8)
    {
        ditherArt = 8;
    }
    else if (ditherScale >= 4)
    {
        ditherArt = 4;
    }
    else if (ditherScale >= 2)
    {
        ditherArt = 2;
    }

    // The inspector edits the stopN properties
    SyncStopsFromProperties();

    // Re-bake only when an input changed. The bake is the expensive part
    // (per-pixel trig for radial/conical); the blit reuses it every frame.
    uint64_t key = ComputeBakeKey(widthPx, heightPx);
    key = (key ^ static_cast<uint64_t>(artW)) * 1099511628211ull;
    key = (key ^ static_cast<uint64_t>(artH)) * 1099511628211ull;
    key = (key ^ static_cast<uint64_t>(ditherArt)) * 1099511628211ull;
    // A bake already refused is not attempted again until an input changes.
    if (m_BakeFailed && m_BakeFailedKey == key)
    {
        return false;
    }

    if (!m_Baked || m_BakeKey != key)
    {
        ForgetBake();
        // A gradient that changes along one axis only is baked as one column
        // or one row, stretched over its rectangle as it is drawn: a
        // full-screen background is a few hundred pixels instead of a
        // screenful.
        bool sameAcross = false;
        bool sameDown = false;
        FindStraightAxes(artW, artH, sameAcross, sameDown);
        const int32_t bakeW = sameAcross ? 1 : widthPx;
        const int32_t bakeH = sameDown ? 1 : heightPx;
        const size_t need = static_cast<size_t>(bakeW) * static_cast<size_t>(bakeH) * 2;  // RGB565
        // Allocate() leaves an unchanged size alone, so the common path costs
        // nothing. A full bake is the object's size in pixels times two: 150
        // KB for a full-screen one at 320x240, more on a bigger panel, and a
        // device without PSRAM can simply refuse it.
        if (!m_Baked.Allocate(need, Deki::Memory::External))
        {
            DEKI_LOG_WARNING("GradientComponent: no room for a %dx%d bake (%u bytes); "
                             "not drawing it",
                             (int)bakeW, (int)bakeH, (unsigned)need);
            m_BakeFailed = true;
            m_BakeFailedKey = key;
            return false;
        }

        if (!RenderToBuffer(m_Baked.Data(), bakeW, bakeH, artW, artH, ditherArt))
        {
            m_Baked.Reset();
            m_BakeFailed = true;
            m_BakeFailedKey = key;
            return false;
        }
        m_BakeFailed = false;
        m_BakeWidth = bakeW;
        m_BakeHeight = bakeH;
        CheckBakeUniformity(bakeW, bakeH);
        m_BakeKey = key;
    }

    outSource = QuadBlit::MakeSource(m_Baked.Data(), m_BakeWidth, m_BakeHeight, QuadBlit::PixelLayout::RGB565(),
                                     false  // ownsPixels: the component owns its bake
    );
    outSource.stretchWidth = m_BakeWidth != widthPx ? widthPx : 0;
    outSource.stretchHeight = m_BakeHeight != heightPx ? heightPx : 0;
    outSource.uniformRows = m_UniformRows;
    outSource.uniformColumns = m_UniformColumns;
    // Drawn bigger than its art (a camera closer than the art's density), a
    // full bake is magnified on every blit; QuadBlit keeps a copy at the drawn
    // size instead. The bake stays as it is until ForgetBake. A strip is
    // filled as it is drawn and needs none.
    outSource.cacheScaled = outSource.stretchWidth == 0 && outSource.stretchHeight == 0;
    outSource.rowColors = m_UniformRows && m_RowColors ? m_RowColors.Data() : nullptr;
    // The full bake's density: the renderer then scales it by the object's
    // scale alone, 1:1 for an unscaled object.
    outSource.pixelsPerMeter = static_cast<float>(widthPx) / width;

    // Centre pivot
    outPivotX = 0.5f;
    outPivotY = 0.5f;

    // White: no tint
    outTintR = outTintG = outTintB = outTintA = 255;

    return true;
}

}  // namespace Deki2D
