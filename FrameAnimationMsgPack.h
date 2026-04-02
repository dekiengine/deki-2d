#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @brief MessagePack-based frame animation format
 *
 * References sprite frames by GUID rather than pixel coordinates.
 * Uses msgpack-c library for both reading and writing.
 * Supports multiple animations per file (e.g., idle, walk, run).
 *
 * Format (MessagePack map):
 * {
 *   "v": 2,                    // version
 *   "s": "spritesheet-guid",   // spritesheet_guid
 *   "a": [                     // animations array
 *     {
 *       "n": "idle",           // animation name
 *       "l": true,             // loop
 *       "f": [                 // frames array
 *         {"g": "frame-guid-1", "d": 100},
 *         {"g": "frame-guid-2", "d": 100}
 *       ]
 *     },
 *     {
 *       "n": "walk",
 *       "l": true,
 *       "f": [...]
 *     }
 *   ]
 * }
 */

// Forward declarations
struct FrameAnimationData;

/**
 * @brief Frame animation file format version
 */
constexpr uint16_t FRAMEANIM_MSGPACK_VERSION = 2;

/**
 * @brief File extension for frame animation files
 */
constexpr const char* FRAMEANIM_EXTENSION = ".frameanim";

/**
 * @brief Helper class for frame animation MessagePack format
 */
class FrameAnimationMsgPackHelper
{
public:
    /**
     * @brief Load frame animation from MessagePack format
     * @param msgpack_path Path to .frameanim MessagePack file
     * @param out_data Output animation data structure
     * @return true on success
     */
    static bool LoadAnimation(const char* msgpack_path, FrameAnimationData* out_data);

    /**
     * @brief Load frame animation from memory buffer
     * @param data Pointer to MessagePack data
     * @param size Size of data in bytes
     * @param out_data Output animation data structure
     * @return true on success
     */
    static bool LoadAnimationFromMemory(const uint8_t* data, size_t size, FrameAnimationData* out_data);

#ifdef DEKI_EDITOR
    /**
     * @brief Save frame animation to MessagePack format (editor only)
     * @param msgpack_path Output path for .frameanim file
     * @param anim_data Animation data to save
     * @return true on success
     */
    static bool SaveAnimation(const char* msgpack_path, const FrameAnimationData* anim_data);
#endif
};
