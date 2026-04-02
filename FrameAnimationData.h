#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "reflection/DekiProperty.h"

/**
 * @brief Single frame reference in a frame animation
 * References a sprite frame sub-asset by GUID
 */
DEKI_SERIALIZABLE
struct FrameAnimFrame
{
    std::string frame_guid;   // GUID of the sprite frame sub-asset
    int32_t duration;         // Duration in milliseconds
};

/**
 * @brief A single animation sequence (e.g., "idle", "walk", "run")
 * Contains a list of frames and playback settings
 */
DEKI_SERIALIZABLE
struct FrameAnimSequence
{
    std::string name;                       // Animation name (e.g., "idle", "walk")
    std::vector<FrameAnimFrame> frames;     // Ordered frame sequence
    bool loop;                              // Whether animation loops
};

/**
 * @brief Frame animation data file containing multiple animations
 * All animations share the same spritesheet
 */
DEKI_SERIALIZABLE
struct FrameAnimationData
{
    /// Asset type name for AssetManager::Load<T>() lookup
    static constexpr const char* AssetTypeName = "Animation";

    std::string spritesheet_guid;               // Parent spritesheet texture GUID
    std::vector<FrameAnimSequence> animations;  // List of animation sequences
};

// Generated property metadata
#include "generated/FrameAnimFrame.gen.h"
#include "generated/FrameAnimSequence.gen.h"
#include "generated/FrameAnimationData.gen.h"
