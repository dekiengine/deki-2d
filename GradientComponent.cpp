#include "GradientComponent.h"
#include <cstring>
#include <deki/Object.h>
#include <deki/Engine.h>
#include "deki-rendering/CameraComponent.h"
#ifndef DEKI_EDITOR
#include <deki/providers/Memory.h>
#endif
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Component Registration
// ============================================================================
// NOTE: s_Properties[] and s_ComponentMeta are now auto-generated in
// GradientComponent.gen.h (included at end of GradientComponent.h)


// Bayer dithering matrices for ordered dithering
static const uint8_t BAYER_2x2[4] = {0, 2, 3, 1};

static const uint8_t BAYER_4x4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static const uint8_t BAYER_8x8[64] = {0,  32, 8,  40, 2,  34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
                                      12, 44, 4,  36, 14, 46, 6,  38, 60, 28, 52, 20, 62, 30, 54, 22,
                                      3,  35, 11, 43, 1,  33, 9,  41, 51, 19, 59, 27, 49, 17, 57, 25,
                                      15, 47, 7,  39, 13, 45, 5,  37, 63, 31, 55, 23, 61, 29, 53, 21};


// 16x16 Bayer matrix (256 values, 0-255 range)
static const uint8_t BAYER_16x16[256] = {
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
    154, 90,  166, 102, 150, 86,  169, 105, 153, 89,  165, 101, 149, 85};


GradientComponent::GradientComponent(float w, float h)
: RendererComponent()
, gradientType(GradientType::Linear)
, tileMode(GradientTileMode::None)
, ditherMode(GradientDitherMode::Ordered4x4)
, ditherScale(1)
, width(w)
, height(h)
, angle(0.0f)
, centerX(0.5f)
, centerY(0.5f)
, radius(0.5f)
, stopCount(2)
, stop1Position(0.0f)
, stop1Color(Deki::Color::White)
, stop2Position(1.0f)
, stop2Color(Deki::Color::Black)
, stop3Position(0.0f)
, stop3Color(Deki::Color::Black)
, stop4Position(0.0f)
, stop4Color(Deki::Color::Black)
, tileWidth(0.0f)
, tileHeight(0.0f)
{
    // Initialize stops array with default white-to-black gradient
    stops[0] = GradientStop(0.0f, Deki::Color::White);
    stops[1] = GradientStop(1.0f, Deki::Color::Black);
    stops[2] = GradientStop();
    stops[3] = GradientStop();
}

GradientComponent::~GradientComponent()
{
    delete[] m_Baked;
}

void GradientComponent::WriteStopsToProperties()
{
    if (stopCount >= 1) { stop1Position = stops[0].position; stop1Color = stops[0].color; }
    if (stopCount >= 2) { stop2Position = stops[1].position; stop2Color = stops[1].color; }
    if (stopCount >= 3) { stop3Position = stops[2].position; stop3Color = stops[2].color; }
    if (stopCount >= 4) { stop4Position = stops[3].position; stop4Color = stops[3].color; }
}

uint64_t GradientComponent::ComputeBakeKey(int32_t widthPx, int32_t heightPx) const
{
    // FNV-1a over a byte snapshot of everything RenderToBuffer reads.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* data, size_t size)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
            h = (h ^ p[i]) * 1099511628211ull;
    };
    auto mixf = [&mix](float f) { uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); mix(&bits, sizeof(bits)); };
    const uint8_t type = static_cast<uint8_t>(gradientType);
    const uint8_t tile = static_cast<uint8_t>(tileMode);
    const uint8_t dither = static_cast<uint8_t>(ditherMode);
    mix(&type, 1); mix(&tile, 1); mix(&dither, 1); mix(&ditherScale, 1);
    mix(&widthPx, sizeof(widthPx)); mix(&heightPx, sizeof(heightPx));
    mixf(angle); mixf(centerX); mixf(centerY); mixf(radius);
    mixf(tileWidth); mixf(tileHeight);
    mix(&stopCount, 1);
    for (int i = 0; i < stopCount && i < MAX_STOPS; ++i)
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

void GradientComponent::SetLinearGradient(float angle_radians)
{
    gradientType = GradientType::Linear;
    angle = angle_radians;  // Store directly in radians (engine convention)
}

void GradientComponent::SetRadialGradient(float center_x_pos, float center_y_pos, float radius_val)
{
    gradientType = GradientType::Radial;
    centerX = std::clamp(center_x_pos, 0.0f, 1.0f);
    centerY = std::clamp(center_y_pos, 0.0f, 1.0f);
    radius = std::clamp(radius_val, 0.0f, 1.0f);
}

void GradientComponent::AddColorStop(float position, uint8_t r, uint8_t g, uint8_t b)
{
    if (stopCount >= MAX_STOPS) return;

    stops[stopCount] = GradientStop(std::clamp(position, 0.0f, 1.0f), r, g, b);
    stopCount++;

    // Sort stops by position (simple bubble sort for small arrays)
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
    // Sync individual property members to stops array
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

void GradientComponent::SetSimpleGradient(
    uint8_t start_r, uint8_t start_g, uint8_t start_b, uint8_t end_r, uint8_t end_g, uint8_t end_b)
{
    ClearColorStops();
    AddColorStop(0.0f, start_r, start_g, start_b);
    AddColorStop(1.0f, end_r, end_g, end_b);
}

void GradientComponent::SetTiling(GradientTileMode mode, float tile_w, float tile_h)
{
    tileMode = mode;
    tileWidth = tile_w;
    tileHeight = tile_h;
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

DEKI_FAST_ATTR float GradientComponent::CalculateGradientPosition(float norm_x, float norm_y) const
{
    switch (gradientType)
    {
        case GradientType::Linear:
        {
            // angle is in radians (engine convention). cos/sin are computed
            // once per bake in RenderToBuffer, not per pixel.
            const float cos_a = m_CosAngle;
            const float sin_a = m_SinAngle;
            float projection = norm_x * cos_a + norm_y * sin_a;
            // For a unit square [0,1]x[0,1], projection ranges from min_proj to max_proj
            // min_proj = min(0, cos_a) + min(0, sin_a)
            // max_proj = max(0, cos_a) + max(0, sin_a)
            float min_proj = std::min(0.0f, cos_a) + std::min(0.0f, sin_a);
            float max_proj = std::max(0.0f, cos_a) + std::max(0.0f, sin_a);
            // Normalize to [0,1] range
            return std::clamp((projection - min_proj) / (max_proj - min_proj), 0.0f, 1.0f);
        }

        case GradientType::Radial:
        {
            float dx = norm_x - centerX;
            float dy = norm_y - centerY;
            float distance = sqrtf(dx * dx + dy * dy);
            return std::clamp(distance / radius, 0.0f, 1.0f);
        }

        case GradientType::Conical:
        {
            float dx = norm_x - centerX;
            float dy = norm_y - centerY;
            float angle_rad = atan2f(dy, dx) + M_PI;  // 0 to 2π
            return angle_rad / (2.0f * M_PI);
        }

        default:
            return norm_x;
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

    // Find the two stops to interpolate between
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

    // Find interpolation range
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
    // Returns Bayer threshold in [0, 1) for ordered dithering. Pixelorama uses
    // the same M / N² normalisation (no +0.5 centring), which gives crisp
    // hard-edged transitions at t=0 and t=1.
    switch (ditherMode)
    {
        case GradientDitherMode::Ordered2x2:
            return BAYER_2x2[(y & 1) * 2 + (x & 1)] / 4.0f;
        case GradientDitherMode::Ordered4x4:
            return BAYER_4x4[(y & 3) * 4 + (x & 3)] / 16.0f;
        case GradientDitherMode::Ordered8x8:
            return BAYER_8x8[(y & 7) * 8 + (x & 7)] / 64.0f;
        case GradientDitherMode::Ordered16x16:
            return BAYER_16x16[(y & 15) * 16 + (x & 15)] / 256.0f;
        default:
            return 0.0f;
    }
}

DEKI_FAST_ATTR void GradientComponent::PickStopByThreshold(float position, float threshold,
                                                            uint8_t* r, uint8_t* g, uint8_t* b) const
{
    // Pixelorama-style gradient dithering: instead of blending between stops,
    // pick ONE of the two bracket stops based on whether the local-t exceeds
    // a Bayer threshold. This produces the stippled, pixel-art look where
    // every pixel is exactly one of the authored colors and the dither pattern
    // fills the transition zones between them.
    //
    // Matches Pixelorama's Gradient.gdshader behaviour:
    //   - position < stops[0].position  → first stop (solid)
    //   - position >= stops[N-1].position → last stop (solid)
    //   - inside a bracket: ramp_val = (local_t < threshold) ? 0 : 1
    //     (i.e. local_t >= threshold picks the upper stop)
    if (stopCount == 0) { *r = *g = *b = 0; return; }
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

            // Pixelorama's comparison: ramp_val = (local_t < threshold) ? 0 : 1
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

void GradientComponent::RenderPixel(
    int32_t x, int32_t y, uint16_t color, uint8_t* render_buffer, int screen_width, int screen_height) const
{
    if (x < 0 || x >= screen_width || y < 0 || y >= screen_height) return;

    uint16_t* buffer16 = (uint16_t*)render_buffer;
    buffer16[y * screen_width + x] = color;
}

void GradientComponent::RenderToBuffer(uint8_t* buffer)
{
    if (!buffer || stopCount == 0) return;

    // width/height are world meters; rasterization runs in pixels.
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t widthPx = static_cast<int32_t>(width * ppm);
    const int32_t heightPx = static_cast<int32_t>(height * ppm);
    if (widthPx <= 0 || heightPx <= 0) return;

    // Sync color stops from property members (for editor serialization)
    SyncStopsFromProperties();
    m_CosAngle = cosf(angle);
    m_SinAngle = sinf(angle);

    uint16_t* buffer16 = reinterpret_cast<uint16_t*>(buffer);
    int32_t render_width = widthPx;
    int32_t render_height = heightPx;

    int32_t tile_w_px = static_cast<int32_t>(tileWidth * ppm);
    int32_t tile_h_px = static_cast<int32_t>(tileHeight * ppm);
    int32_t actual_tile_width = tile_w_px > 0 ? tile_w_px : render_width;
    int32_t actual_tile_height = tile_h_px > 0 ? tile_h_px : render_height;

    // Snap ditherScale down to the nearest power of 2 and convert to a shift,
    // so the per-pixel cost stays a single bit-shift (no integer division on
    // the Xtensa hot path).
    int dither_shift = 0;
    {
        uint8_t s = ditherScale;
        if (s >= 16)     dither_shift = 4;
        else if (s >= 8) dither_shift = 3;
        else if (s >= 4) dither_shift = 2;
        else if (s >= 2) dither_shift = 1;
    }

    for (int32_t y = 0; y < render_height; y++)
    {
        for (int32_t x = 0; x < render_width; x++)
        {
            float norm_x = 0.0f, norm_y = 0.0f;

            // Handle tiling
            switch (tileMode)
            {
                case GradientTileMode::None:
                    norm_x = (float)x / render_width;
                    norm_y = (float)y / render_height;
                    break;

                case GradientTileMode::Horizontal:
                {
                    int32_t tile_x = x % actual_tile_width;
                    norm_x = (float)tile_x / actual_tile_width;
                    norm_y = (float)y / render_height;
                    break;
                }

                case GradientTileMode::Vertical:
                {
                    int32_t tile_y = y % actual_tile_height;
                    norm_x = (float)x / render_width;
                    norm_y = (float)tile_y / actual_tile_height;
                    break;
                }

                case GradientTileMode::Both:
                {
                    int32_t tile_x = x % actual_tile_width;
                    int32_t tile_y = y % actual_tile_height;
                    norm_x = (float)tile_x / actual_tile_width;
                    norm_y = (float)tile_y / actual_tile_height;
                    break;
                }

                case GradientTileMode::Mirror:
                {
                    int32_t tile_x = x % (actual_tile_width * 2);
                    int32_t tile_y = y % (actual_tile_height * 2);
                    norm_x = tile_x < actual_tile_width
                                 ? (float)tile_x / actual_tile_width
                                 : 1.0f - (float)(tile_x - actual_tile_width) / actual_tile_width;
                    norm_y = tile_y < actual_tile_height
                                 ? (float)tile_y / actual_tile_height
                                 : 1.0f - (float)(tile_y - actual_tile_height) / actual_tile_height;
                    break;
                }
            }

            // Calculate gradient position
            float grad_pos = CalculateGradientPosition(norm_x, norm_y);

            uint8_t r, g, b;
            if (ditherMode == GradientDitherMode::None)
            {
                // No dither: smooth lerp between stops (legacy behaviour).
                InterpolateColor(grad_pos, &r, &g, &b);
            }
            else
            {
                // Pixelorama-style stipple dither: pick one of the bracketing
                // stop colors based on a Bayer threshold. Coordinates are
                // shifted by dither_shift so each Bayer cell spans an N×N block.
                float threshold = SampleBayerThreshold(x >> dither_shift, y >> dither_shift);
                PickStopByThreshold(grad_pos, threshold, &r, &g, &b);
            }

            // Convert to RGB565 and write directly to buffer at (x, y)
            buffer16[y * render_width + x] = ConvertToRGB565(r, g, b);
        }
    }
}

bool GradientComponent::RenderContent(const Deki::Object* owner,
                                       QuadBlit::Source& outSource,
                                       float& outPivotX,
                                       float& outPivotY,
                                       uint8_t& outTintR,
                                       uint8_t& outTintG,
                                       uint8_t& outTintB,
                                       uint8_t& outTintA)
{
    if (!owner || stopCount == 0) return false;

    // width/height are world meters; raster buffer sized in pixels via ppm.
    const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
    const int32_t widthPx = static_cast<int32_t>(width * ppm);
    const int32_t heightPx = static_cast<int32_t>(height * ppm);
    if (widthPx <= 0 || heightPx <= 0) return false;

    // Sync color stops from property members (for editor serialization)
    SyncStopsFromProperties();

    // Re-bake only when an input changed. The bake is the expensive part
    // (per-pixel trig for radial/conical); the blit reuses it every frame.
    const uint64_t key = ComputeBakeKey(widthPx, heightPx);
    const size_t need = static_cast<size_t>(widthPx) * static_cast<size_t>(heightPx) * 2;  // RGB565
    if (!m_Baked || m_BakedSize != need || m_BakeKey != key)
    {
        if (m_BakedSize != need)
        {
            delete[] m_Baked;
            m_Baked = new uint8_t[need];
            m_BakedSize = need;
        }
        RenderToBuffer(m_Baked);
        m_BakeKey = key;
    }

    // Create source descriptor
    outSource = QuadBlit::MakeSource(
        m_Baked,
        widthPx,
        heightPx,
        2,      // bytesPerPixel for RGB565
        false,  // hasAlpha - gradients don't have alpha
        true,   // isRGB565
        false   // ownsPixels - the component owns its bake
    );
    outSource.pixelsPerMeter = Deki::EngineSettings::Global().pixelsPerMeter;

    // Gradient uses center pivot (0.5, 0.5)
    outPivotX = 0.5f;
    outPivotY = 0.5f;

    // No tint for gradients (white = no modification)
    outTintR = outTintG = outTintB = outTintA = 255;

    return true;
}
