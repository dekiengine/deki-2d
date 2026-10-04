#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <deki/reflection/Property.h>

namespace Deki2D
{

/// One frame of a frame animation: a sprite frame sub-asset, by GUID, and how
/// long it is shown.
struct DEKI_SERIALIZABLE FrameAnimFrame
{
    std::string frameGuid;  // GUID of the sprite frame sub-asset
    int32_t duration;       // Duration in milliseconds
};

/// One named animation, such as "idle" or "walk": its frames in order and
/// whether it loops.
struct DEKI_SERIALIZABLE FrameAnimSequence
{
    std::string name;
    std::vector<FrameAnimFrame> frames;
    bool loop;
};

/// A frame animation asset: several animations that all use the same
/// spritesheet.
struct DEKI_SERIALIZABLE FrameAnimationData
{
    /// Asset type name for AssetManager::Load<T>() lookup
    static constexpr const char* kAssetTypeName = "Animation";

    std::string spritesheetGuid;  // GUID of the spritesheet texture
    std::vector<FrameAnimSequence> animations;
};

#include "generated/FrameAnimFrame.gen.h"
#include "generated/FrameAnimSequence.gen.h"
#include "generated/FrameAnimationData.gen.h"

}  // namespace Deki2D
