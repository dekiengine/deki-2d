#pragma once

#include <cstdint>
#include <cstddef>

namespace Deki2D
{

/**
 * @brief MessagePack-based frame animation cache format
 *
 * References sprite frames by GUID rather than pixel coordinates.
 * Supports multiple animations per file (e.g., idle, walk, run).
 *
 * The cache format is generated, not hand-written: FrameAnimationData is a
 * DEKI_SERIALIZABLE struct, so the reflection codegen emits Deki::Serialize<T> (save)
 * and DeserializeMsgPack (load). Keys are the struct field names, hashed the same
 * way as component fields, so the map is:
 *   { "spritesheetGuid": "...", "animations": [
 *       { "name": "idle", "frames": [ { "frameGuid": "...", "duration": 100 } ],
 *         "loop": true } ] }
 * To change the format, change the struct fields and re-export the .anim assets.
 */

// Forward declarations
struct FrameAnimationData;

/**
 * @brief File extension for frame animation files
 */
constexpr const char* kFrameanimExtension = ".anim";

/**
 * @brief Helper class for frame animation MessagePack format
 */
class FrameAnimationMsgPackHelper
{
public:
    /**
     * @brief Load frame animation from MessagePack format
     * @param msgpackPath Path to .frameanim MessagePack file
     * @param outData Output animation data structure
     * @return true on success
     */
    static bool LoadAnimation(const char* msgpackPath, FrameAnimationData* outData);

    /**
     * @brief Load frame animation from memory buffer
     * @param data Pointer to MessagePack data
     * @param size Size of data in bytes
     * @param outData Output animation data structure
     * @return true on success
     */
    static bool LoadAnimationFromMemory(const uint8_t* data, size_t size, FrameAnimationData* outData);

#ifdef DEKI_EDITOR
    /**
     * @brief Save frame animation to MessagePack format (editor only)
     * @param msgpackPath Output path for .frameanim file
     * @param animData Animation data to save
     * @return true on success
     */
    static bool SaveAnimation(const char* msgpackPath, const FrameAnimationData* animData);
#endif
};

// Registers the "Animation" asset loader. Idempotent. Called from
// Deki2DInitSystem (Deki2DInit.h), which is also what gets this file into a
// firmware.
void RegisterAnimationLoader();

}  // namespace Deki2D
