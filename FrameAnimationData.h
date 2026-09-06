#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <deki/reflection/Property.h>

/**
 * @brief Single frame reference in a frame animation
 * References a sprite frame sub-asset by GUID
 */
struct DEKI_SERIALIZABLE FrameAnimFrame
{
    std::string frameGuid;   // GUID of the sprite frame sub-asset
    int32_t duration;         // Duration in milliseconds
};

/**
 * @brief A single animation sequence (e.g., "idle", "walk", "run")
 * Contains a list of frames and playback settings
 */
struct DEKI_SERIALIZABLE FrameAnimSequence
{
    std::string name;                       // Animation name (e.g., "idle", "walk")
    std::vector<FrameAnimFrame> frames;     // Ordered frame sequence
    bool loop;                              // Whether animation loops
};

/**
 * @brief Frame animation data file containing multiple animations
 * All animations share the same spritesheet
 */
struct DEKI_SERIALIZABLE FrameAnimationData
{
    /// Asset type name for AssetManager::Load<T>() lookup
    static constexpr const char* AssetTypeName = "Animation";

    std::string spritesheetGuid;               // Parent spritesheet texture GUID
    std::vector<FrameAnimSequence> animations;  // List of animation sequences
};

// Generated property metadata
#include "generated/FrameAnimFrame.gen.h"
#include "generated/FrameAnimSequence.gen.h"
#include "generated/FrameAnimationData.gen.h"
