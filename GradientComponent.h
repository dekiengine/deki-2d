#pragma once

#include <stdint.h>
#include "deki-rendering/RendererComponent.h"
#include <deki/Color.h>

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
    Deki::Color color;  // Color at this position

    GradientStop(float pos = 0.0f, const Deki::Color& col = Deki::Color::Black)
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
    DEKI_DESCRIPTION("Draws a procedural gradient: linear, radial or conical.")

    // Gradient properties
    DEKI_EXPORT
    GradientType gradientType;
    DEKI_EXPORT
    GradientTileMode tileMode;
    DEKI_EXPORT
    GradientDitherMode ditherMode;

    /**
     * @brief Scale of the dither pattern in pixels per Bayer cell.
     *
     * 1 (default) = native 1px dither (current behavior).
     * 2/4/8/16    = chunkier blocks that make the pattern more visible —
     *               useful for stylized retro looks.
     * Non power-of-2 values snap down to the nearest power of 2 (so 3→2, 7→4).
     */
    DEKI_EXPORT
    uint8_t ditherScale;

    // Area to fill (meters)
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float width;
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float height;

    // Gradient parameters
    DEKI_EXPORT
    DEKI_UNIT(Angle)
    float angle;  // Linear gradient angle. Stored in radians (0 = horizontal, π/2 = vertical); inspector displays degrees.
    DEKI_EXPORT
    float centerX;  // For radial/conical gradients (0.0 to 1.0)
    DEKI_EXPORT
    float centerY;  // For radial/conical gradients (0.0 to 1.0)
    DEKI_EXPORT
    float radius;  // For radial gradients (0.0 to 1.0)

    // Color stops (up to 4 stops for memory efficiency)
    static constexpr uint8_t MAX_STOPS = 4;
    GradientStop stops[MAX_STOPS];

    DEKI_EXPORT
    uint8_t stopCount;

    // Individual color stop properties for editor serialization
    // Stop 1 is always visible (minimum 1 stop required)
    DEKI_EXPORT
    float stop1Position;
    DEKI_EXPORT
    Deki::Color stop1Color;

    // Stop 2 visible when stopCount >= 2
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stopCount, 2)
    float stop2Position;
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stopCount, 2)
    Deki::Color stop2Color;

    // Stop 3 visible when stopCount >= 3
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stopCount, 3)
    float stop3Position;
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stopCount, 3)
    Deki::Color stop3Color;

    // Stop 4 visible when stopCount >= 4
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stopCount, 4)
    float stop4Position;
    DEKI_EXPORT
    DEKI_VISIBLE_WHEN(stopCount, 4)
    Deki::Color stop4Color;

    // Tile properties (world meters; 0 = use component width/height)
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float tileWidth;
    DEKI_EXPORT
    DEKI_UNIT(Distance)
    float tileHeight;

    GradientComponent(float w = 0.0f, float h = 0.0f);
    ~GradientComponent();

    /**
     * @brief Set gradient type and basic parameters
     */
    void SetGradientType(GradientType type);

    /**
     * @brief Set linear gradient with angle
     * @param angle_radians Angle in radians (0 = left to right, π/2 = top to bottom)
     */
    void SetLinearGradient(float angle_radians = 0.0f);

    /**
     * @brief Set radial gradient with center and radius
     * @param centerX Center X position (0.0 to 1.0)
     * @param centerY Center Y position (0.0 to 1.0)
     * @param radius Radius (0.0 to 1.0)
     */
    void SetRadialGradient(float centerX = 0.5f, float centerY = 0.5f, float radius = 0.5f);

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
    void SetTiling(GradientTileMode mode, float tile_w = 0.0f, float tile_h = 0.0f);

    /**
     * @brief Set dithering mode for smooth gradients on RGB565
     */
    void SetDithering(GradientDitherMode mode);

    /**
     * @brief Set the area to fill
     */
    void SetArea(float w, float h);

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
    // Culling extents (meters): the box the content is baked into.
    bool GetContentExtents(float& outWidth, float& outHeight) const override
    {
        if (width <= 0.0f || height <= 0.0f)
            return false;
        outWidth = width;
        outHeight = height;
        return true;
    }

    bool RenderContent(const Deki::Object* owner,
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
     * @brief Sample the Bayer threshold at (x, y) for the current ditherMode.
     * @return Threshold in [0, 1).
     */
    float SampleBayerThreshold(int32_t x, int32_t y) const;

    /**
     * @brief Pixelorama-style stipple pick: write one of the bracketing stop
     *        colors based on whether the local-t exceeds a Bayer threshold.
     *        No interpolation between stops — every output pixel is exactly
     *        one of the authored colors.
     */
    void PickStopByThreshold(float position, float threshold,
                             uint8_t* r, uint8_t* g, uint8_t* b) const;

    /**
     * @brief Convert RGB to RGB565 format
     */
    uint16_t ConvertToRGB565(uint8_t r, uint8_t g, uint8_t b) const;

    /**
     * @brief Render single pixel with bounds checking
     */
    void RenderPixel(
        int32_t x, int32_t y, uint16_t color, uint8_t* render_buffer, int screen_width, int screen_height) const;

    // Baked pixels for the current property set (what RenderContent hands to
    // QuadBlit). Keyed by a hash of every input so the gradient is rasterised
    // only when something changed; it used to be rasterised into a freshly
    // allocated buffer on every frame (with cos/sin/atan2 per pixel) and freed
    // by the renderer right after.
    uint8_t* m_Baked = nullptr;
    size_t m_BakedSize = 0;
    uint64_t m_BakeKey = 0;
    uint64_t ComputeBakeKey(int32_t widthPx, int32_t heightPx) const;

    // Linear-gradient trig, computed once per bake instead of once per pixel.
    float m_CosAngle = 1.0f;
    float m_SinAngle = 0.0f;

    // Keep the reflected stopN fields equal to stops[] after a programmatic
    // change, so SyncStopsFromProperties() (which runs before every bake) does
    // not overwrite AddColorStop()/SetSimpleGradient() with stale values.
    void WriteStopsToProperties();
};

// Generated property metadata (after class definition for offsetof)
#include "generated/GradientComponent.gen.h"