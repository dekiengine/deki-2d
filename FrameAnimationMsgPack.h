#pragma once

#include <cstdint>
#include <cstddef>

namespace Deki2D
{

// The MessagePack cache format for frame animations. Frames are named by
// sprite frame GUID, not pixel coordinates, and one file holds several
// animations (such as idle, walk, run).
//
// The format comes from the struct: FrameAnimationData is DEKI_SERIALIZABLE,
// so the reflection codegen emits Deki::Serialize<T> (save) and
// DeserializeMsgPack (load). Keys are the struct field names, hashed the same
// way as component fields, so the map is:
//   { "spritesheetGuid": "...", "animations": [
//       { "name": "idle", "frames": [ { "frameGuid": "...", "duration": 100 } ],
//         "loop": true } ] }
// To change the format, change the struct fields and re-export the .anim assets.

struct FrameAnimationData;

constexpr const char* kFrameanimExtension = ".anim";

/// Reads and writes frame animations in the MessagePack format.
class FrameAnimationMsgPackHelper
{
public:
    /// Loads the animation file at `msgpackPath` into `outData`. Returns false
    /// on failure.
    static bool LoadAnimation(const char* msgpackPath, FrameAnimationData* outData);

    /// Loads an animation from MessagePack bytes in memory into `outData`.
    /// Returns false on failure.
    static bool LoadAnimationFromMemory(const uint8_t* data, size_t size, FrameAnimationData* outData);

#ifdef DEKI_EDITOR
    /// Writes `animData` to `msgpackPath`. Editor only. Returns false on
    /// failure.
    static bool SaveAnimation(const char* msgpackPath, const FrameAnimationData* animData);
#endif
};

/// Registers the "Animation" asset loader. Safe to call more than once. Called
/// from Deki2DInitSystem (Deki2DInit.h), which is also what links this file
/// into a firmware.
void RegisterAnimationLoader();

}  // namespace Deki2D
