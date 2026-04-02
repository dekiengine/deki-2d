#include "GradientComponent.h"
#include "DekiObject.h"
#include "../rendering/CameraComponent.h"
#ifndef DEKI_EDITOR
#include "providers/DekiMemoryProvider.h"
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


GradientComponent::GradientComponent(int32_t w, int32_t h)
: RendererComponent()
, gradient_type(GradientType::Linear)
, tile_mode(GradientTileMode::None)
, dither_mode(GradientDitherMode::Ordered4x4)
, width(w)
, height(h)
, angle(0.0f)
, center_x(0.5f)
, center_y(0.5f)
, radius(0.5f)
, stop_count(2)
, stop1_position(0.0f)
, stop1_color(deki::Color::White)
, stop2_position(1.0f)
, stop2_color(deki::Color::Black)
, stop3_position(0.0f)
, stop3_color(deki::Color::Black)
, stop4_position(0.0f)
, stop4_color(deki::Color::Black)
, tile_width(0)
, tile_height(0)
{
    // Initialize stops array with default white-to-black gradient
    stops[0] = GradientStop(0.0f, deki::Color::White);
    stops[1] = GradientStop(1.0f, deki::Color::Black);
    stops[2] = GradientStop();
    stops[3] = GradientStop();
}

GradientComponent::~GradientComponent()
{
}

void GradientComponent::SetGradientType(GradientType type)
{
    gradient_type = type;
}

void GradientComponent::SetLinearGradient(float angle_degrees)
{
    gradient_type = GradientType::Linear;
    angle = angle_degrees;  // Store directly in degrees
}

void GradientComponent::SetRadialGradient(float center_x_pos, float center_y_pos, float radius_val)
{
    gradient_type = GradientType::Radial;
    center_x = std::clamp(center_x_pos, 0.0f, 1.0f);
    center_y = std::clamp(center_y_pos, 0.0f, 1.0f);
    radius = std::clamp(radius_val, 0.0f, 1.0f);
}

void GradientComponent::AddColorStop(float position, uint8_t r, uint8_t g, uint8_t b)
{
    if (stop_count >= MAX_STOPS) return;

    stops[stop_count] = GradientStop(std::clamp(position, 0.0f, 1.0f), r, g, b);
    stop_count++;

    // Sort stops by position (simple bubble sort for small arrays)
    for (int i = 0; i < stop_count - 1; i++)
    {
        for (int j = 0; j < stop_count - i - 1; j++)
        {
            if (stops[j].position > stops[j + 1].position)
            {
                GradientStop temp = stops[j];
                stops[j] = stops[j + 1];
                stops[j + 1] = temp;
            }
        }
    }
}

void GradientComponent::ClearColorStops()
{
    stop_count = 0;
}

void GradientComponent::SyncStopsFromProperties()
{
    // Sync individual property members to stops array
    if (stop_count >= 1)
    {
        stops[0].position = stop1_position;
        stops[0].color = stop1_color;
    }
    if (stop_count >= 2)
    {
        stops[1].position = stop2_position;
        stops[1].color = stop2_color;
    }
    if (stop_count >= 3)
    {
        stops[2].position = stop3_position;
        stops[2].color = stop3_color;
    }
    if (stop_count >= 4)
    {
        stops[3].position = stop4_position;
        stops[3].color = stop4_color;
    }
}

void GradientComponent::SetSimpleGradient(
    uint8_t start_r, uint8_t start_g, uint8_t start_b, uint8_t end_r, uint8_t end_g, uint8_t end_b)
{
    ClearColorStops();
    AddColorStop(0.0f, start_r, start_g, start_b);
    AddColorStop(1.0f, end_r, end_g, end_b);
}

void GradientComponent::SetTiling(GradientTileMode mode, int32_t tile_w, int32_t tile_h)
{
    tile_mode = mode;
    tile_width = tile_w;
    tile_height = tile_h;
}

void GradientComponent::SetDithering(GradientDitherMode mode)
{
    dither_mode = mode;
}

void GradientComponent::SetArea(int32_t w, int32_t h)
{
    width = w;
    height = h;
}

DEKI_FAST_ATTR float GradientComponent::CalculateGradientPosition(float norm_x, float norm_y) const
{
    switch (gradient_type)
    {
        case GradientType::Linear:
        {
            // Convert angle from degrees to radians
            float angle_rad = angle * (float)M_PI / 180.0f;
            // Project point onto line defined by angle
            float cos_a = cosf(angle_rad);
            float sin_a = sinf(angle_rad);
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
            float dx = norm_x - center_x;
            float dy = norm_y - center_y;
            float distance = sqrtf(dx * dx + dy * dy);
            return std::clamp(distance / radius, 0.0f, 1.0f);
        }

        case GradientType::Conical:
        {
            float dx = norm_x - center_x;
            float dy = norm_y - center_y;
            float angle_rad = atan2f(dy, dx) + M_PI;  // 0 to 2π
            return angle_rad / (2.0f * M_PI);
        }

        default:
            return norm_x;
    }
}

DEKI_FAST_ATTR void GradientComponent::InterpolateColor(float position, uint8_t* r, uint8_t* g, uint8_t* b) const
{
    if (stop_count == 0)
    {
        *r = *g = *b = 0;
        return;
    }

    if (stop_count == 1)
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

    if (position >= stops[stop_count - 1].position)
    {
        *r = stops[stop_count - 1].color.r;
        *g = stops[stop_count - 1].color.g;
        *b = stops[stop_count - 1].color.b;
        return;
    }

    // Find interpolation range
    for (int i = 0; i < stop_count - 1; i++)
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

void GradientComponent::ApplyDithering(int32_t x, int32_t y, uint8_t* r, uint8_t* g, uint8_t* b) const
{
    if (dither_mode == GradientDitherMode::None) return;

    // Get normalized threshold from Bayer matrix (0.0 to 1.0 range)
    // The threshold determines whether a pixel rounds up or down during quantization
    float threshold = 0.0f;

    switch (dither_mode)
    {
        case GradientDitherMode::Ordered2x2:
            // BAYER_2x2 has values 0-3, normalize to 0.0-1.0
            threshold = BAYER_2x2[(y & 1) * 2 + (x & 1)] / 4.0f;
            break;

        case GradientDitherMode::Ordered4x4:
            // BAYER_4x4 has values 0-15, normalize to 0.0-1.0
            threshold = BAYER_4x4[(y & 3) * 4 + (x & 3)] / 16.0f;
            break;

        case GradientDitherMode::Ordered8x8:
            // BAYER_8x8 has values 0-63, normalize to 0.0-1.0
            threshold = BAYER_8x8[(y & 7) * 8 + (x & 7)] / 64.0f;
            break;

        case GradientDitherMode::Ordered16x16:
            // BAYER_16x16 has values 0-255, normalize to 0.0-1.0
            threshold = BAYER_16x16[(y & 15) * 16 + (x & 15)] / 256.0f;
            break;

        default:
            return;
    }

    // Apply ordered dithering for RGB565 quantization
    // RGB565: R=5 bits (step=8), G=6 bits (step=4), B=5 bits (step=8)
    // Add threshold * step_size to color before quantization
    // This causes some pixels to round up, creating the dither pattern

    // For 5-bit channels: quantization step is 8 (256/32)
    // For 6-bit channel: quantization step is 4 (256/64)
    int16_t new_r = *r + (int16_t)(threshold * 8.0f) - 4;  // Center around 0
    int16_t new_g = *g + (int16_t)(threshold * 4.0f) - 2;  // Center around 0
    int16_t new_b = *b + (int16_t)(threshold * 8.0f) - 4;  // Center around 0

    *r = (uint8_t)std::clamp((int)new_r, 0, 255);
    *g = (uint8_t)std::clamp((int)new_g, 0, 255);
    *b = (uint8_t)std::clamp((int)new_b, 0, 255);
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
    if (!buffer || stop_count == 0) return;
    if (width <= 0 || height <= 0) return;

    // Sync color stops from property members (for editor serialization)
    SyncStopsFromProperties();

    uint16_t* buffer16 = reinterpret_cast<uint16_t*>(buffer);
    int32_t render_width = width;
    int32_t render_height = height;

    int32_t actual_tile_width = tile_width > 0 ? tile_width : render_width;
    int32_t actual_tile_height = tile_height > 0 ? tile_height : render_height;

    for (int32_t y = 0; y < render_height; y++)
    {
        for (int32_t x = 0; x < render_width; x++)
        {
            float norm_x = 0.0f, norm_y = 0.0f;

            // Handle tiling
            switch (tile_mode)
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

            // Interpolate color
            uint8_t r, g, b;
            InterpolateColor(grad_pos, &r, &g, &b);

            // Apply dithering
            ApplyDithering(x, y, &r, &g, &b);

            // Convert to RGB565 and write directly to buffer at (x, y)
            buffer16[y * render_width + x] = ConvertToRGB565(r, g, b);
        }
    }
}

bool GradientComponent::RenderContent(const DekiObject* owner,
                                       QuadBlit::Source& outSource,
                                       float& outPivotX,
                                       float& outPivotY,
                                       uint8_t& outTintR,
                                       uint8_t& outTintG,
                                       uint8_t& outTintB,
                                       uint8_t& outTintA)
{
    if (!owner || stop_count == 0) return false;
    if (width <= 0 || height <= 0) return false;

    // Sync color stops from property members (for editor serialization)
    SyncStopsFromProperties();

    // Allocate buffer for RGB565 gradient
    size_t bufferSize = width * height * 2;  // RGB565 = 2 bytes per pixel
    uint8_t* buffer = new uint8_t[bufferSize];

    // Render gradient to buffer at 1x scale
    RenderToBuffer(buffer);

    // Create source descriptor
    outSource = QuadBlit::MakeSource(
        buffer,
        width,
        height,
        2,      // bytesPerPixel for RGB565
        false,  // hasAlpha - gradients don't have alpha
        true,   // isRGB565
        true    // ownsPixels - caller should free
    );

    // Gradient uses center pivot (0.5, 0.5)
    outPivotX = 0.5f;
    outPivotY = 0.5f;

    // No tint for gradients (white = no modification)
    outTintR = outTintG = outTintB = outTintA = 255;

    return true;
}
