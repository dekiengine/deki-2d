#include "GradientComponent.h"
#include <cstring>
#include <deki/Object.h>
#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include "deki-rendering/CameraComponent.h"
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
            // cos/sin of the angle are computed once per bake in
            // RenderToBuffer, not per pixel.
            const float cos_a = m_CosAngle;
            const float sin_a = m_SinAngle;
            float projection = normX * cos_a + normY * sin_a;
            // Over the unit square the projection runs from min_proj to max_proj
            float min_proj = std::min(0.0f, cos_a) + std::min(0.0f, sin_a);
            float max_proj = std::max(0.0f, cos_a) + std::max(0.0f, sin_a);
            return std::clamp((projection - min_proj) / (max_proj - min_proj), 0.0f, 1.0f);
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

void GradientComponent::RenderToBuffer(uint8_t* buffer, int32_t outW, int32_t outH, int32_t artW, int32_t artH,
                                       int32_t ditherArt)
{
    if (!buffer || stopCount == 0)
    {
        return;
    }

    // width/height are world meters; the layout runs on the art grid.
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t widthPx = artW;
    const int32_t heightPx = artH;
    if (widthPx <= 0 || heightPx <= 0 || outW <= 0 || outH <= 0)
    {
        return;
    }
    if (ditherArt < 1)
    {
        ditherArt = 1;
    }

    // The inspector edits the stopN properties
    SyncStopsFromProperties();
    m_CosAngle = cosf(angle);
    m_SinAngle = sinf(angle);

    uint16_t* buffer16 = reinterpret_cast<uint16_t*>(buffer);
    int32_t renderWidth = widthPx;
    int32_t renderHeight = heightPx;

    int32_t tileWPx = static_cast<int32_t>(tileWidth * ppm);
    int32_t tileHPx = static_cast<int32_t>(tileHeight * ppm);
    int32_t actualTileWidth = tileWPx > 0 ? tileWPx : renderWidth;
    int32_t actualTileHeight = tileHPx > 0 ? tileHPx : renderHeight;

    for (int32_t oy = 0; oy < outH; oy++)
    {
        // The art row this output row covers (the same row for every output
        // row of a whole-number upscale).
        const int32_t y = static_cast<int32_t>((static_cast<int64_t>(oy) * renderHeight) / outH);
        const int32_t cellY = y / ditherArt;
        for (int32_t ox = 0; ox < outW; ox++)
        {
            const int32_t x = static_cast<int32_t>((static_cast<int64_t>(ox) * renderWidth) / outW);
            float normX = 0.0f, normY = 0.0f;

            switch (tileMode)
            {
                case GradientTileMode::None:
                    normX = (float)x / renderWidth;
                    normY = (float)y / renderHeight;
                    break;

                case GradientTileMode::Horizontal:
                {
                    int32_t tileX = x % actualTileWidth;
                    normX = (float)tileX / actualTileWidth;
                    normY = (float)y / renderHeight;
                    break;
                }

                case GradientTileMode::Vertical:
                {
                    int32_t tileY = y % actualTileHeight;
                    normX = (float)x / renderWidth;
                    normY = (float)tileY / actualTileHeight;
                    break;
                }

                case GradientTileMode::Both:
                {
                    int32_t tileX = x % actualTileWidth;
                    int32_t tileY = y % actualTileHeight;
                    normX = (float)tileX / actualTileWidth;
                    normY = (float)tileY / actualTileHeight;
                    break;
                }

                case GradientTileMode::Mirror:
                {
                    int32_t tileX = x % (actualTileWidth * 2);
                    int32_t tileY = y % (actualTileHeight * 2);
                    normX = tileX < actualTileWidth ? (float)tileX / actualTileWidth
                                                    : 1.0f - (float)(tileX - actualTileWidth) / actualTileWidth;
                    normY = tileY < actualTileHeight ? (float)tileY / actualTileHeight
                                                     : 1.0f - (float)(tileY - actualTileHeight) / actualTileHeight;
                    break;
                }
            }

            float gradPos = CalculateGradientPosition(normX, normY);

            uint8_t r, g, b;
            if (ditherMode == GradientDitherMode::None)
            {
                // No dither: blend smoothly between stops
                InterpolateColor(gradPos, &r, &g, &b);
            }
            else
            {
                // Stipple dither: pick one of the two stops around the position
                // by a Bayer threshold, read on the art grid like the colour
                // (see RenderToBuffer in the header): zooming magnifies the
                // device's pattern instead of laying out a new one.
                float threshold = SampleBayerThreshold(x / ditherArt, cellY);
                PickStopByThreshold(gradPos, threshold, &r, &g, &b);
            }

            buffer16[oy * outW + ox] = ConvertToRGB565(r, g, b);
        }
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
    // project's pixels per meter); the bake is at the density the view draws
    // it at, so it lands 1:1 on the screen (see RenderToBuffer).
    const float artPPM = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t artW = static_cast<int32_t>(width * artPPM);
    const int32_t artH = static_cast<int32_t>(height * artPPM);
    if (artW <= 0 || artH <= 0)
    {
        return false;
    }

    float bakePPM = artPPM;
    const DekiRendering::DrawView& view = DekiRendering::CurrentDrawView();
    if (view.pixelsPerMeter > 0.0f)
    {
        bakePPM = view.pixelsPerMeter;
        // At most twice the view's own area: zoomed far in (the editor's
        // scene view), a big gradient would otherwise ask for a bake many
        // screens wide. Past that it is scaled up a little as it is drawn.
        const double viewArea = static_cast<double>(view.width) * view.height;
        const double bakeArea = static_cast<double>(width) * bakePPM * height * bakePPM;
        if (viewArea > 0.0 && bakeArea > viewArea * 2.0)
        {
            bakePPM *= static_cast<float>(std::sqrt(viewArea * 2.0 / bakeArea));
        }
    }
    const int32_t widthPx = std::max<int32_t>(1, static_cast<int32_t>(std::lround(width * bakePPM)));
    const int32_t heightPx = std::max<int32_t>(1, static_cast<int32_t>(std::lround(height * bakePPM)));

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
    const size_t need = static_cast<size_t>(widthPx) * static_cast<size_t>(heightPx) * 2;  // RGB565
    // A size already refused is not attempted again.
    if (m_BakeFailedSize == need)
    {
        return false;
    }

    if (!m_Baked || m_Baked.Bytes() != need || m_BakeKey != key)
    {
        // Allocate() leaves an unchanged size alone, so the common path costs
        // nothing. The bake is the object's size in pixels times two: 150 KB
        // for a full-screen gradient at 320x240, more on a bigger panel, and a
        // device without PSRAM can simply refuse it.
        if (!m_Baked.Allocate(need, Deki::Memory::External))
        {
            DEKI_LOG_WARNING("GradientComponent: no room for a %dx%d bake (%u bytes); "
                             "not drawing it",
                             (int)widthPx, (int)heightPx, (unsigned)need);
            m_BakeFailedSize = need;
            return false;
        }
        m_BakeFailedSize = 0;

        RenderToBuffer(m_Baked.Data(), widthPx, heightPx, artW, artH, ditherArt);
        m_BakeKey = key;
    }

    outSource = QuadBlit::MakeSource(m_Baked.Data(), widthPx, heightPx, QuadBlit::PixelLayout::RGB565(),
                                     false  // ownsPixels: the component owns its bake
    );
    // The bake's own density: the renderer then scales it by the object's
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
