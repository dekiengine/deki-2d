#pragma once
#include "Sprite.h"

namespace Deki2D
{

/// The interface every sprite loader implements. Each implementation defines
/// its own format constants.
class ISpriteLoader
{
public:
    virtual ~ISpriteLoader() = default;

    /// Loads a sprite from a file. Returns nullptr on failure.
    virtual Sprite* LoadFromFile(const char* filePath) = 0;

    /// The name of the format this loader reads.
    virtual const char* GetFormat() const = 0;

    /// Registers this loader with the sprite system.
    virtual void RegisterLoader() = 0;
};

}  // namespace Deki2D