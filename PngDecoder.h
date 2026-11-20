#pragma once

// A PNG reader of our own, for pictures an app gets while it runs (a store's
// screenshots, an avatar), where nothing was baked at build time.
//
// It reads the picture row by row and shrinks it while reading, so a large
// picture never needs its full size in memory: what it keeps is the picture
// it gives back, two rows of the original and the 32 KiB the inflater looks
// back over. It reads every PNG that is not interlaced (each colour type and
// bit depth, palettes, transparency), and refuses one it cannot read rather
// than guessing.

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Deki2D
{

namespace Png
{

enum class Result : uint8_t
{
    Ok,
    NotPng,       // not a PNG at all
    Unsupported,  // a PNG this reader does not read (interlaced)
    Corrupt,      // broken, cut short, or contradicting itself
    TooLarge,     // wider or taller than kMaxSide
    NoMemory,
};

/// The widest and tallest picture read.
constexpr uint32_t kMaxSide = 8192;

/// What a PNG's header says.
struct Info
{
    uint32_t width = 0;
    uint32_t height = 0;
    bool hasAlpha = false;  // an alpha channel, or a transparent colour
};

/// Reads the header only.
Result ReadInfo(const uint8_t* data, size_t size, Info& out);

/// The size a `width` x `height` picture is read at to fit in
/// `maxWidth` x `maxHeight`: the same when it fits, otherwise shrunk keeping
/// its shape (never enlarged). 0 for a limit means none.
void FitSize(uint32_t width, uint32_t height, uint32_t maxWidth, uint32_t maxHeight, uint32_t& outWidth,
             uint32_t& outHeight);

/// One row of the picture read, `width` pixels of RGBA, 8 bits each, not
/// premultiplied.
using RowSink = std::function<void(uint32_t y, const uint8_t* rgba, uint32_t width)>;

/// Reads the picture at the size FitSize gives, handing each row to `row` in
/// order, top to bottom. A shrunk pixel is the average of the pixels it
/// covers, weighted by their alpha. `outWidth` and `outHeight` are set before
/// the first row.
Result Decode(const uint8_t* data, size_t size, uint32_t maxWidth, uint32_t maxHeight, const RowSink& row,
              uint32_t& outWidth, uint32_t& outHeight);

/// A name for a result, for logs.
const char* ResultName(Result result);

}  // namespace Png

}  // namespace Deki2D
