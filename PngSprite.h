#pragma once

#include "PngDecoder.h"
#include "Sprite.h"

#include <cstddef>
#include <cstdint>

namespace Deki2D
{

/// A sprite from a PNG in memory (a picture downloaded while the app runs),
/// shrunk to fit `maxWidth` x `maxHeight` keeping its shape (0 for no limit).
/// RGB565 when the picture is opaque, RGB565A8 when it has transparency. The
/// caller owns the sprite. nullptr when the PNG cannot be read; `result`, when
/// given, says why.
Sprite* CreateSpriteFromPng(const uint8_t* data, size_t size, int32_t maxWidth, int32_t maxHeight,
                            Png::Result* result = nullptr);

}  // namespace Deki2D
