#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <deki/assets/Texture2D.h>

namespace Deki2D
{

/// A frame of a spritesheet. Its GUID lets an AssetRef<Sprite> point at the
/// frame on its own.
struct SpriteFrame
{
    char guid[37];  // 36 chars + null terminator (UUID format)
    int32_t x;      // X position in parent texture
    int32_t y;      // Y position in parent texture
    int32_t width;
    int32_t height;

    // The frame's own 9-slice borders, measured inside the frame.
    bool hasNineSlice = false;
    uint16_t nineSliceLeft = 0;
    uint16_t nineSliceRight = 0;
    uint16_t nineSliceTop = 0;
    uint16_t nineSliceBottom = 0;
};

/// Sprite metadata stored in texture files.
struct SpriteMetadata
{
    float pivotX;  // Pivot point X (0.0 to 1.0)
    float pivotY;  // Pivot point Y (0.0 to 1.0)
    float pixelsPerMeter;
    uint8_t transparentR;         // Transparent color R (if hasTransparency)
    uint8_t transparentG;         // Transparent color G (if hasTransparency)
    uint8_t transparentB;         // Transparent color B (if hasTransparency)
    uint8_t hasNineSlice;         // 1 if sprite has 9-slice data, 0 otherwise
    uint16_t nineSliceLeft;       // 9-slice: pixels from left edge
    uint16_t nineSliceRight;      // 9-slice: pixels from right edge
    uint16_t nineSliceTop;        // 9-slice: pixels from top edge
    uint16_t nineSliceBottom;     // 9-slice: pixels from bottom edge
    uint16_t defaultFrameWidth;   // Spritesheet frame width (0 = use full width)
    uint16_t defaultFrameHeight;  // Spritesheet frame height (0 = use full height)
};

/// A Texture2D with sprite metadata: pivot, scale, chroma key, 9-slice
/// borders and spritesheet frames.
class Sprite : public Deki::Texture2D
{
public:
    /// Asset type name for AssetManager::Load<T>() lookup
    static constexpr const char* kAssetTypeName = "Sprite";

    float pivotX;          // Pivot point X (0.0 to 1.0, default 0.5)
    float pivotY;          // Pivot point Y (0.0 to 1.0, default 0.5)
    float pixelsPerMeter;  // default 16, the project default
    uint8_t transparentR;  // chroma key colour
    uint8_t transparentG;
    uint8_t transparentB;

    // Chroma key (1-bit transparency): when on, pixels of colour
    // transparentR/G/B draw as transparent. For RGB565/RGB565A8 sources the
    // key is quantized to 5/6/5 at load time so it matches the pixels.
    bool hasChromaKey;
    // Per-row spans of non-key columns (pairs [start, end] per row), which
    // let QuadBlit skip the per-pixel key compare. Same layout as
    // alphaRowSpans. Empty when there is no chroma key or the file has no spans.
    Deki::Buffer<int16_t> chromaRowSpans;  // owning; see Deki::Texture2D::alphaRowSpans

    // 9-slice, for UI elements that scale.
    bool hasNineSlice;
    uint16_t nineSliceLeft;    // Pixels from left edge to start of center region
    uint16_t nineSliceRight;   // Pixels from right edge to start of center region
    uint16_t nineSliceTop;     // Pixels from top edge to start of center region
    uint16_t nineSliceBottom;  // Pixels from bottom edge to start of center region

    // Above 0, the sprite is a spritesheet with frames of this size.
    int32_t defaultFrameWidth;   // 0 = use full width
    int32_t defaultFrameHeight;  // 0 = use full height

    // Spritesheet frames, from the .dtex metadata.
    std::vector<SpriteFrame> frames;

    // The image's size when the texture is stored smaller than it (Max
    // Size), 0 otherwise. The file's frames and nine-slice borders are in
    // image pixels; the loader converts them, and pixelsPerMeter, to stored
    // pixels, so the sprite keeps its size in the world. Anything else that
    // addresses the image in its own pixels (a tileset) maps them with
    // SourceToStoredX/Y.
    int32_t sourceWidth;
    int32_t sourceHeight;
    // Stored pixels per image pixel: width / sourceWidth, 1 when not shrunk.
    float sourceScale;

    /// An image-pixel coordinate (a rect edge) in stored pixels.
    int32_t SourceToStoredX(int32_t v) const { return SourceToStored(v, sourceWidth, width); }
    int32_t SourceToStoredY(int32_t v) const { return SourceToStored(v, sourceHeight, height); }

    /// round(v * stored / source): the rule the editor's encoder resamples
    /// frames with (DekiEditor::SourceToStored), so the two agree.
    static int32_t SourceToStored(int32_t v, int32_t sourceSize, int32_t storedSize)
    {
        if (sourceSize <= 0 || sourceSize == storedSize)
        {
            return v;
        }
        return static_cast<int32_t>((static_cast<int64_t>(v) * storedSize * 2 + sourceSize) /
                                    (2 * static_cast<int64_t>(sourceSize)));
    }

    Sprite();
    virtual ~Sprite();

    /// The frame with this GUID, or nullptr.
    const SpriteFrame* FindFrame(const std::string& guid) const;

    /// Loads a sprite from a texture file. Returns nullptr on failure.
    static Sprite* Load(const char* filePath);

    /// Loads a sprite from the bytes of a .dtex file (header, pixels,
    /// metadata) already in memory, as in a pack file. Returns nullptr on failure.
    static Sprite* LoadFromFileData(const uint8_t* data, size_t size);

    /// For a texture stored smaller than its image: converts the frames, the
    /// default frame size, the nine-slice borders and pixelsPerMeter to
    /// stored pixels. The loaders call it with the SourceSize chunk; it does
    /// nothing when the image size is the stored size.
    void ApplySourceSize(int32_t imageWidth, int32_t imageHeight);

    /// A new RGB565 sprite filled with one colour.
    static Sprite* CreateSolid(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b);

    /// A new RGB565A8 sprite filled with one colour and alpha (0 transparent, 255 opaque).
    static Sprite* CreateSolidRGBA(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /// A new sprite of the target size, filled by repeating `source`.
    static Sprite* CreateTiled(Sprite* source, int32_t targetWidth, int32_t targetHeight);

    /// A new sprite of the target size, scaled with 9-slice: the corners keep
    /// their size and only the edges and centre stretch.
    ///
    ///   +---+-------+---+
    ///   | TL|  Top  | TR|  TL/TR/BL/BR = Corners (never scaled)
    ///   +---+-------+---+  Top/Bottom = Scaled horizontally only
    ///   |   |       |   |  Left/Right = Scaled vertically only
    ///   | L | Center| R |  Center = Scaled both directions
    ///   |   |       |   |
    ///   +---+-------+---+
    ///   | BL| Bottom| BR|
    ///   +---+-------+---+
    ///
    /// `source` must have 9-slice data, and the target must be at least the
    /// sum of the borders. Returns nullptr otherwise.
    static Sprite* CreateNineSlice(Sprite* source, int32_t targetWidth, int32_t targetHeight);

    /// CreateTiled() into the caller's buffer of dstW * dstH pixels in the
    /// source's format. SpriteComponent's render-mode cache uses it so a
    /// resize does not allocate a new Sprite. `source` must be valid and not empty.
    static void BakeTiledInto(uint8_t* dst, int32_t dstW, int32_t dstH, const Sprite* source);

    /// CreateNineSlice() into the caller's buffer of dstW * dstH pixels in
    /// the source's format. The caller checks that the source has 9-slice
    /// data and that the target is at least the sum of the borders.
    static void BakeNineSliceInto(uint8_t* dst, int32_t dstW, int32_t dstH, const Sprite* source);

    /// A rectangle of a sprite (a frame, or all of it) and the 9-slice
    /// borders inside that rectangle.
    struct SliceRegion
    {
        int32_t x = 0, y = 0, width = 0, height = 0;
        uint16_t left = 0, right = 0, top = 0, bottom = 0;
    };

    /// The two bakes above, reading only `region` of the source. The caller
    /// checks that the region lies inside the source and that its borders
    /// leave a center.
    static void BakeTiledRegion(uint8_t* dst, int32_t dstW, int32_t dstH, const Sprite* source,
                                const SliceRegion& region);
    static void BakeNineSliceRegion(uint8_t* dst, int32_t dstW, int32_t dstH, const Sprite* source,
                                    const SliceRegion& region);

    /// Turns on 9-slice with these borders, in pixels, replacing any from the
    /// file. Returns false when the borders do not fit the sprite.
    bool SetNineSliceBorders(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom);

protected:
    /// `data` is the pixel data after the header.
    bool LoadFromMemory(const Deki::Texture2D::Header& header, const uint8_t* data) override;

private:
    void SetDefaultSpriteProperties();
};

}  // namespace Deki2D
