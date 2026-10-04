#include "FrameAnimationMsgPack.h"
#include "FrameAnimationData.h"
#include <deki/LogSystem.h>
#include <deki/assets/AssetManager.h>
#include <deki/providers/FileSystem.h>
#include <deki/SceneMessagePack.h>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>

#ifdef DEKI_EDITOR
#include <nlohmann/json.hpp>
#include <deki/reflection/Serialization.h>
#endif

namespace Deki2D
{

#ifdef DEKI_EDITOR
// Editor only: saving goes through the generated reflection serializer.
using json = nlohmann::json;
#endif

// FrameAnimationData is DEKI_SERIALIZABLE, so the reflection codegen
// generates the editor's JSON Deki::Serialize<T> and DeserializeMsgPack for
// every platform (declared in generated/FrameAnimationData.gen.h, which
// FrameAnimationData.h includes). Load and save both use them.

bool FrameAnimationMsgPackHelper::LoadAnimation(const char* msgpackPath, FrameAnimationData* outData)
{
    if (!msgpackPath || !outData)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimation - null parameters");
        return false;
    }

    // Through the filesystem provider, like Sprite and BitmapFont: on a device
    // and in the desktop simulator the path starts with a mount such as "S:/",
    // which a plain ifstream cannot resolve.
    Deki::IFileSystem* fs = Deki::FileSystem::GetFileSystemForPath(msgpackPath);
    if (!fs)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimation - no filesystem for: %s", msgpackPath);
        return false;
    }
    Deki::IFileSystem::FileHandle file = fs->OpenFile(msgpackPath, Deki::IFileSystem::OpenMode::ReadBinary);
    if (!file)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimation - failed to open: %s", msgpackPath);
        return false;
    }
    const long size = fs->GetFileSize(file);
    if (size <= 0)
    {
        fs->CloseFile(file);
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimation - empty file: %s", msgpackPath);
        return false;
    }
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    const size_t read = fs->ReadFile(file, buffer.data(), static_cast<size_t>(size));
    fs->CloseFile(file);
    if (read != static_cast<size_t>(size))
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimation - failed to read file: %s", msgpackPath);
        return false;
    }
    return LoadAnimationFromMemory(buffer.data(), static_cast<size_t>(size), outData);
}

bool FrameAnimationMsgPackHelper::LoadAnimationFromMemory(const uint8_t* data, size_t size, FrameAnimationData* outData)
{
    if (!data || size == 0 || !outData)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimationFromMemory - invalid parameters");
        return false;
    }

    outData->spritesheetGuid.clear();
    outData->animations.clear();

    // The generated deserializer, keyed by full field names. Desktop and
    // device both read through SceneMsgPackParser.
    Deki::SceneFormat::SceneMsgPackParser parser(data, size);
    uint32_t mapSize = 0;
    if (!parser.ReadMapSize(mapSize))
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimationFromMemory - root is not a MessagePack map");
        return false;
    }
    if (!DeserializeMsgPack(*outData, parser, mapSize))
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimationFromMemory - deserialize failed");
        return false;
    }

    int totalFrames = 0;
    for (const auto& anim : outData->animations)
    {
        totalFrames += static_cast<int>(anim.frames.size());
    }

    DEKI_LOG_INTERNAL("FrameAnimationMsgPackHelper::LoadAnimation - loaded %d animations with %d total frames",
                      static_cast<int>(outData->animations.size()), totalFrames);

    return true;
}

#ifdef DEKI_EDITOR
bool FrameAnimationMsgPackHelper::SaveAnimation(const char* msgpackPath, const FrameAnimationData* animData)
{
    if (!msgpackPath || !animData)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::SaveAnimation - null parameters");
        return false;
    }

    try
    {
        // The generated serializer, keyed by full field names, then MessagePack.
        // Mirrors the load path; nested sequences and frames are included.
        json j = Deki::Serialize<FrameAnimationData>(*animData);
        std::vector<uint8_t> msgpackData = json::to_msgpack(j);

        std::ofstream file(msgpackPath, std::ios::binary);
        if (!file.is_open())
        {
            DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::SaveAnimation - failed to open for writing: %s", msgpackPath);
            return false;
        }

        file.write(reinterpret_cast<const char*>(msgpackData.data()), msgpackData.size());

        int totalFrames = 0;
        for (const auto& seq : animData->animations)
        {
            totalFrames += static_cast<int>(seq.frames.size());
        }

        DEKI_LOG_INTERNAL(
            "FrameAnimationMsgPackHelper::SaveAnimation - saved %d animations with %d total frames (%zu bytes)",
            static_cast<int>(animData->animations.size()), totalFrames, msgpackData.size());

        return file.good();
    }
    catch (const std::exception& e)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::SaveAnimation - error: %s", e.what());
        return false;
    }
}
#endif

// Registers the animation loader with AssetManager. memLoader lets a device
// load packed animations straight from a .dpack.
//
// Must be called, not a static registrar: a firmware links the game from an
// archive, and the linker drops an object nothing references, registrar and
// all, which leaves animations unloadable on the device.
namespace
{
bool s_AnimLoaderRegistered = false;
}

void RegisterAnimationLoader()
{
    if (s_AnimLoaderRegistered)
    {
        return;
    }
    s_AnimLoaderRegistered = true;

    auto loader = [](const char* p) -> void*
    {
        auto* data = new FrameAnimationData();
        if (FrameAnimationMsgPackHelper::LoadAnimation(p, data))
        {
            return data;
        }
        delete data;
        return nullptr;
    };
    auto unloader = [](void* a) { delete static_cast<FrameAnimationData*>(a); };
    auto memLoader = [](const uint8_t* d, size_t n) -> void*
    {
        auto* data = new FrameAnimationData();
        if (FrameAnimationMsgPackHelper::LoadAnimationFromMemory(d, n, data))
        {
            return data;
        }
        delete data;
        return nullptr;
    };
    Deki::AssetManager::RegisterLoader("FrameAnimationData", loader, unloader, memLoader);
    Deki::AssetManager::RegisterLoader("Animation", loader, unloader, memLoader);
}

}  // namespace Deki2D
