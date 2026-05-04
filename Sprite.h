#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Texture2D.h"

/**
 * @brief A frame within a spritesheet
 *
 * Each frame has its own GUID for addressable sub-assets.
 * The GUID allows individual frames to be referenced in AssetRef<Sprite>.
 */
struct SpriteFrame
{
    char guid[37];  // 36 chars + null terminator (UUID format)
    int32_t x;      // X position in parent texture
    int32_t y;      // Y position in parent texture
    int32_t width;  // Frame width
    int32_t height; // Frame height
};

/**
 * @brief Sprite metadata stored in .tex files
 */
struct SpriteMetadata
{
    float pivot_x;  // Pivot point X (0.0 to 1.0)
    float pivot_y;  // Pivot point Y (0.0 to 1.0)
    float pixels_per_meter;  // Pixels per world unit (for scaling)
    uint8_t transparent_r;  // Transparent color R (if has_transparency)
    uint8_t transparent_g;  // Transparent color G (if has_transparency)
    uint8_t transparent_b;  // Transparent color B (if has_transparency)
    uint8_t has_nine_slice;  // 1 if sprite has 9-slice data, 0 otherwise
    uint16_t nine_slice_left;    // 9-slice: pixels from left edge
    uint16_t nine_slice_right;   // 9-slice: pixels from right edge
    uint16_t nine_slice_top;     // 9-slice: pixels from top edge
    uint16_t nine_slice_bottom;  // 9-slice: pixels from bottom edge
    uint16_t default_frame_width;   // Spritesheet frame width (0 = use full width)
    uint16_t default_frame_height;  // Spritesheet frame height (0 = use full height)
};

/**
 * @brief Represents a 2D sprite - a Texture2D with additional sprite metadata
 */
class Sprite : public Texture2D
{
   public:
    /// Asset type name for AssetManager::Load<T>() lookup
    static constexpr const char* AssetTypeName = "Sprite";

    // Sprite-specific properties
    float pivot_x;  // Pivot point X (0.0 to 1.0, default 0.5)
    float pivot_y;  // Pivot point Y (0.0 to 1.0, default 0.5)
    float pixels_per_meter;  // Pixels per world unit (default 16 = matches project PPM; ignored in camera Pixels mode)
    uint8_t transparent_r;  // Transparent color R
    uint8_t transparent_g;  // Transparent color G
    uint8_t transparent_b;  // Transparent color B

    // Chroma key (1-bit transparency). When has_chroma_key is true, pixels
    // matching (transparent_r/g/b) are treated as transparent at render time.
    // For RGB565/RGB565A8 sources the key is pre-quantized to 5/6/5 precision
    // at load time so it matches pixels extracted from the source.
    bool has_chroma_key;
    // Per-row non-key column spans (packed pairs [start, end] per row), used
    // by QuadBlit to fast-path chroma blits. Owned by the sprite. Layout
    // matches alphaRowSpans. nullptr if has_chroma_key is false or the chunk
    // didn't supply spans (legacy files).
    int16_t* chromaRowSpans;

    // 9-slice properties (for scalable UI elements)
    bool has_nine_slice;     // Whether this sprite has 9-slice data
    uint16_t nine_slice_left;    // Pixels from left edge to start of center region
    uint16_t nine_slice_right;   // Pixels from right edge to start of center region
    uint16_t nine_slice_top;     // Pixels from top edge to start of center region
    uint16_t nine_slice_bottom;  // Pixels from bottom edge to start of center region

    // Spritesheet default frame dimensions (set by editor from .data file)
    // If > 0, indicates this is a spritesheet with frames of this size
    int32_t default_frame_width;   // Default frame width (0 = use full width)
    int32_t default_frame_height;  // Default frame height (0 = use full height)

    // Frame list for spritesheets (loaded from .dtex metadata)
    // Each frame has its own GUID for sub-asset addressing
    std::vector<SpriteFrame> frames;

    Sprite();
    virtual ~Sprite();

    /**
     * @brief Find a frame by its GUID
     * @param guid The GUID to search for
     * @return Pointer to the frame if found, nullptr otherwise
     */
    const SpriteFrame* FindFrame(const std::string& guid) const;

    /**
     * @brief Load sprite from V-Engine texture format (.tex)
     * @param file_path Path to the .tex file
     * @return Pointer to loaded sprite or nullptr on failure
     */
    static Sprite* Load(const char* file_path);

    /**
     * @brief Load sprite from raw file data in memory (for pack file support)
     * @param data Pointer to raw .dtex file bytes (header + pixel data + metadata)
     * @param size Total size in bytes
     * @return Loaded sprite or nullptr on failure
     */
    static Sprite* LoadFromFileData(const uint8_t* data, size_t size);

    /**
     * @brief Creates a solid color sprite
     * @param width Width of the sprite
     * @param height Height of the sprite
     * @param r Red color value (0-255)
     * @param g Green color value (0-255)
     * @param b Blue color value (0-255)
     * @return A pointer to the created Sprite
     */
    static Sprite* CreateSolid(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Creates a solid color sprite with alpha channel
     * @param width Width of the sprite
     * @param height Height of the sprite
     * @param r Red color value (0-255)
     * @param g Green color value (0-255)
     * @param b Blue color value (0-255)
     * @param a Alpha value (0-255, 0=transparent, 255=opaque)
     * @return A pointer to the created Sprite
     */
    static Sprite* CreateSolidRGBA(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /**
     * @brief Creates a tiled sprite from a source sprite to fill target dimensions
     * @param source The source sprite to tile
     * @param target_width Target width to fill
     * @param target_height Target height to fill
     * @return A pointer to the created tiled Sprite
     */
    static Sprite* CreateTiled(Sprite* source, int32_t target_width, int32_t target_height);

    /**
     * @brief Creates a scaled sprite using 9-slice/9-patch technique
     *
     * 9-slice (also known as 9-patch) allows scaling UI elements while preserving
     * corner details and preventing stretching artifacts. The sprite is divided into
     * 9 regions:
     *
     *   +---+-------+---+
     *   | TL|  Top  | TR|  TL/TR/BL/BR = Corners (never scaled)
     *   +---+-------+---+  Top/Bottom = Scaled horizontally only
     *   |   |       |   |  Left/Right = Scaled vertically only
     *   | L | Center| R |  Center = Scaled both directions
     *   |   |       |   |
     *   +---+-------+---+
     *   | BL| Bottom| BR|
     *   +---+-------+---+
     *
     * @param source The source sprite with 9-slice data (has_nine_slice must be true)
     * @param target_width Target width (must be >= nine_slice_left + nine_slice_right)
     * @param target_height Target height (must be >= nine_slice_top + nine_slice_bottom)
     * @return A pointer to the scaled sprite, or nullptr on failure
     */
    static Sprite* CreateNineSlice(Sprite* source, int32_t target_width, int32_t target_height);

    /**
     * @brief Tile a source sprite into a caller-owned pixel buffer.
     *
     * Same algorithm as CreateTiled() but writes into an externally managed
     * buffer. Used by SpriteComponent's render-mode cache to avoid allocating
     * a new Sprite per resize.
     *
     * @param dst Destination buffer of size dst_w * dst_h * bytesPerPixel(source->format)
     * @param dst_w Destination width
     * @param dst_h Destination height
     * @param source Source sprite to tile (must be valid, dimensions > 0)
     */
    static void BakeTiledInto(uint8_t* dst, int32_t dst_w, int32_t dst_h, const Sprite* source);

    /**
     * @brief 9-slice scale a source sprite into a caller-owned pixel buffer.
     *
     * Same 9-region algorithm as CreateNineSlice() but writes into an
     * externally managed buffer. Caller must ensure source->has_nine_slice
     * is true and dst_w/dst_h >= nine_slice border totals.
     *
     * @param dst Destination buffer of size dst_w * dst_h * bytesPerPixel(source->format)
     * @param dst_w Destination width
     * @param dst_h Destination height
     * @param source Source sprite with valid 9-slice metadata
     */
    static void BakeNineSliceInto(uint8_t* dst, int32_t dst_w, int32_t dst_h, const Sprite* source);

    /**
     * @brief Sets 9-slice borders for this sprite
     *
     * This enables 9-slice scaling for the sprite. Use this for sprites loaded
     * from files that don't have 9-slice metadata, or to override existing metadata.
     *
     * @param left Left border in pixels
     * @param right Right border in pixels
     * @param top Top border in pixels
     * @param bottom Bottom border in pixels
     * @return true if borders are valid for this sprite's dimensions
     */
    bool SetNineSliceBorders(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom);

   protected:
    /**
     * @brief Load sprite data from memory buffer (includes sprite metadata)
     * @param header Parsed texture header
     * @param data Raw file data after header
     * @return true on success
     */
    bool LoadFromMemory(const Texture2D::Header& header, const uint8_t* data) override;

   private:
    /**
     * @brief Set default sprite properties
     */
    void SetDefaultSpriteProperties();
};
