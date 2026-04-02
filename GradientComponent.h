#pragma once

#include <stdint.h>
#include "../rendering/RendererComponent.h"
#include "Color.h"

/**
 * @brief Gradient types for procedural generation
 */
enum class GradientType : uint8_t
{
    Linear = 0,   // Linear gradient with angle parameter
    Radial = 1,   // From center outward
    Conical = 2   // Rotating around center
};

/**
 * @brief Tiling modes for gradient rendering
 */
enum class GradientTileMode : uint8_t
{
    None = 0,  // No tiling - render gradient once
    Horizontal = 1,  // Tile horizontally
    Vertical = 2,  // Tile vertically
    Both = 3,  // Tile both horizontally and vertically
    Mirror = 4  // Mirror tiling (gradient reverses each tile)
};

/**
 * @brief Dithering modes for gradient rendering
 */
enum class GradientDitherMode : uint8_t
{
    None = 0,  // No dithering
    Ordered2x2 = 1,  // 2x2 Bayer matrix (fast, low quality)
    Ordered4x4 = 2,  // 4x4 Bayer matrix (balanced)
    Ordered8x8 = 3,  // 8x8 Bayer matrix (high quality, slower)
    Ordered16x16 = 4  // 16x16 Bayer matrix (highest quality, slowest)
};

/**
 * @brief Color stop for gradient definition
 */
struct GradientStop
{
    float position;  // Position along gradient (0.0 to 1.0)
    deki::Color color;  // Color at this position

    GradientStop(float pos = 0.0f, const deki::Color& col = deki::Color::Black)
    : position(pos), color(col)
    {
    }
    
    // Legacy constructor for backward compatibility
    GradientStop(float pos, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
    : position(pos), color(red, green, blue, alpha)
    {
    }
};

/**
 * @brief Gradient component for procedural gradient rendering
 *
 * Generates gradients in real-time with support for:
 * - Multiple gradient types (linear, radial, conical)
 * - Tiling modes for pattern repetition
 * - Dithering for smooth color transitions on RGB565
 * - Memory-efficient procedural generation
 * - Performance optimized for embedded systems
 */
class GradientComponent : public RendererComponent
{
   public:
    DEKI_COMPONENT(GradientComponent, RendererComponent, "2D", "8d84cca3-7eb9-4b92-ba49-968ceec203c8", "DEKI_FEATURE_GRADIENT")

    // Gradient properties
    DEKI_EXPORT
    GradientType gradient_type;
    DEKI_EXPORT
    GradientTileMode tile_mode;
    DEKI_EXPORT
    GradientDitherMode dither_mode;

    // Area to fill
    DEKI_EXPORT
    int32_t width;
    DEKI_EXPORT
    int32_t height;

    // Gradient parameters
    DEKI_EXPORT
    float angle;  // For linear gradients (in degrees, 0 = horizontal, 90 = vertical)
    DEKI_EXPORT
    float center_x;  // For radial/conical gradients (0.0 to 1.0)
    DEKI_EXPORT
    float center_y;  // For radial/conical gradients (0.0 to 1.0)
    DEKI_EXPORT
    float radius;  // For radial gradients (0.0 to 1.0)

    // Color stops (up to 4 stops for memory efficiency)
    static constexpr uint8_t MAX_STOPS = 4;
    GradientStop stops[MAX_STOPS];

    DEKI_EXPORT
    uint8_t stop_count;

    // Individual color stop properties for editor serialization
    // Stop 1 is always visible (minimum 1 stop required)
    DEKI_EXPORT
    float stop1_position;
    DEKI_EXPORT
    deki::Color stop1_color;

    // Stop 2 visible when stop_count >= 2
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stop_count, 2)
    float stop2_position;
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stop_count, 2)
    deki::Color stop2_color;

    // Stop 3 visible when stop_count >= 3
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stop_count, 3)
    float stop3_position;
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stop_count, 3)
    deki::Color stop3_color;

    // Stop 4 visible when stop_count >= 4
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stop_count, 4)
    float stop4_position;
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stop_count, 4)
    deki::Color stop4_color;

    // Tile properties
    DEKI_EXPORT
    int32_t tile_width;  // Width of individual tile (0 = use component width)
    DEKI_EXPORT
    int32_t tile_height;  // Height of individual tile (0 = use component height)

    GradientComponent(int32_t w = 0, int32_t h = 0);
    ~GradientComponent();

    /**
     * @brief Set gradient type and basic parameters
     */
    void SetGradientType(GradientType type);

    /**
     * @brief Set linear gradient with angle
     * @param angle_degrees Angle in degrees (0 = left to right, 90 = top to bottom)
     */
    void SetLinearGradient(float angle_degrees = 0.0f);

    /**
     * @brief Set radial gradient with center and radius
     * @param center_x Center X position (0.0 to 1.0)
     * @param center_y Center Y position (0.0 to 1.0)
     * @param radius Radius (0.0 to 1.0)
     */
    void SetRadialGradient(float center_x = 0.5f, float center_y = 0.5f, float radius = 0.5f);

    /**
     * @brief Add a color stop to the gradient
     * @param position Position along gradient (0.0 to 1.0)
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     */
    void AddColorStop(float position, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Clear all color stops
     */
    void ClearColorStops();

    /**
     * @brief Sync color stops from individual property members
     * Called before rendering to ensure stops array matches property values
     */
    void SyncStopsFromProperties();

    /**
     * @brief Set simple two-color gradient
     * @param start_r Start color red
     * @param start_g Start color green
     * @param start_b Start color blue
     * @param end_r End color red
     * @param end_g End color green
     * @param end_b End color blue
     */
    void SetSimpleGradient(
        uint8_t start_r, uint8_t start_g, uint8_t start_b, uint8_t end_r, uint8_t end_g, uint8_t end_b);

    /**
     * @brief Set tiling mode
     */
    void SetTiling(GradientTileMode mode, int32_t tile_w = 0, int32_t tile_h = 0);

    /**
     * @brief Set dithering mode for smooth gradients on RGB565
     */
    void SetDithering(GradientDitherMode mode);

    /**
     * @brief Set the area to fill
     */
    void SetArea(int32_t w, int32_t h);

    /**
     * @brief Render gradient to a local buffer at origin (0,0)
     *
     * Use this for editor preview or when you want to render to a texture
     * that will be positioned separately. Buffer size should be width * height * 2 bytes (RGB565).
     *
     * @param buffer Output buffer (RGB565 format)
     */
    void RenderToBuffer(uint8_t* buffer);

    // Unified rendering via QuadBlit
    bool RenderContent(const DekiObject* owner,
                       QuadBlit::Source& outSource,
                       float& outPivotX,
                       float& outPivotY,
                       uint8_t& outTintR,
                       uint8_t& outTintG,
                       uint8_t& outTintB,
                       uint8_t& outTintA) override;

   private:
    /**
     * @brief Calculate gradient value at normalized position
     * @param norm_x Normalized X position (0.0 to 1.0)
     * @param norm_y Normalized Y position (0.0 to 1.0)
     * @return Gradient position (0.0 to 1.0)
     */
    float CalculateGradientPosition(float norm_x, float norm_y) const;

    /**
     * @brief Interpolate color at gradient position
     * @param position Gradient position (0.0 to 1.0)
     * @param r Output red component
     * @param g Output green component
     * @param b Output blue component
     */
    void InterpolateColor(float position, uint8_t* r, uint8_t* g, uint8_t* b) const;

    /**
     * @brief Apply dithering to color
     * @param x Pixel X coordinate
     * @param y Pixel Y coordinate
     * @param r Red component (modified)
     * @param g Green component (modified)
     * @param b Blue component (modified)
     */
    void ApplyDithering(int32_t x, int32_t y, uint8_t* r, uint8_t* g, uint8_t* b) const;

    /**
     * @brief Convert RGB to RGB565 format
     */
    uint16_t ConvertToRGB565(uint8_t r, uint8_t g, uint8_t b) const;

    /**
     * @brief Render single pixel with bounds checking
     */
    void RenderPixel(
        int32_t x, int32_t y, uint16_t color, uint8_t* render_buffer, int screen_width, int screen_height) const;
};

// Generated property metadata (after class definition for offsetof)
#include "generated/GradientComponent.gen.h"