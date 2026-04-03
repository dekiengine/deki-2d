#include "FrameAnimationMsgPack.h"
#include "FrameAnimationData.h"
#include "DekiLogSystem.h"
#include "assets/AssetManager.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef DEKI_EDITOR
// Editor uses nlohmann/json for MessagePack
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#else
// ESP32/Simulator uses mpack for MessagePack
#include <mpack/mpack.h>
#endif

bool FrameAnimationMsgPackHelper::LoadAnimation(const char* msgpack_path, FrameAnimationData* out_data)
{
    if (!msgpack_path || !out_data)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimation - null parameters");
        return false;
    }

    // Read file into buffer
    std::ifstream file(msgpack_path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimation - failed to open: %s", msgpack_path);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimation - failed to read file");
        return false;
    }

    return LoadAnimationFromMemory(buffer.data(), static_cast<size_t>(size), out_data);
}

bool FrameAnimationMsgPackHelper::LoadAnimationFromMemory(const uint8_t* data, size_t size, FrameAnimationData* out_data)
{
    if (!data || size == 0 || !out_data)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimationFromMemory - invalid parameters");
        return false;
    }

    // Clear output
    out_data->spritesheet_guid.clear();
    out_data->animations.clear();

#ifdef DEKI_EDITOR
    // Use nlohmann/json to deserialize MessagePack
    try
    {
        json j = json::from_msgpack(data, data + size);

        // Version check
        uint16_t version = 1;
        if (j.contains("v"))
        {
            version = j["v"].get<uint16_t>();
            if (version > FRAMEANIM_MSGPACK_VERSION)
            {
                DEKI_LOG_ERROR("FrameAnimationMsgPackHelper - unsupported version: %d", version);
                return false;
            }
        }

        // Spritesheet GUID
        if (j.contains("s")) out_data->spritesheet_guid = j["s"].get<std::string>();

        // Version 2: multiple animations
        if (version >= 2 && j.contains("a") && j["a"].is_array())
        {
            for (const auto& anim_obj : j["a"])
            {
                FrameAnimSequence seq;
                seq.loop = true;

                if (anim_obj.contains("n")) seq.name = anim_obj["n"].get<std::string>();
                if (anim_obj.contains("l")) seq.loop = anim_obj["l"].get<bool>();

                if (anim_obj.contains("f") && anim_obj["f"].is_array())
                {
                    for (const auto& frame_obj : anim_obj["f"])
                    {
                        FrameAnimFrame frame;
                        frame.duration = 100;

                        if (frame_obj.contains("g")) frame.frame_guid = frame_obj["g"].get<std::string>();
                        if (frame_obj.contains("d")) frame.duration = frame_obj["d"].get<int32_t>();

                        seq.frames.push_back(frame);
                    }
                }

                out_data->animations.push_back(seq);
            }
        }
        // Version 1 backwards compatibility: single animation
        else if (j.contains("f") && j["f"].is_array())
        {
            FrameAnimSequence seq;
            if (j.contains("n")) seq.name = j["n"].get<std::string>();
            if (j.contains("l")) seq.loop = j["l"].get<bool>();

            for (const auto& frame_obj : j["f"])
            {
                FrameAnimFrame frame;
                frame.duration = 100;

                if (frame_obj.contains("g")) frame.frame_guid = frame_obj["g"].get<std::string>();
                if (frame_obj.contains("d")) frame.duration = frame_obj["d"].get<int32_t>();

                seq.frames.push_back(frame);
            }

            out_data->animations.push_back(seq);
        }
    }
    catch (const std::exception& e)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimationFromMemory - parse error: %s", e.what());
        return false;
    }
#else
    // Use mpack to deserialize MessagePack
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, reinterpret_cast<const char*>(data), size);
    mpack_tree_parse(&tree);

    if (mpack_tree_error(&tree) != mpack_ok)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::LoadAnimationFromMemory - mpack error: %d", (int)mpack_tree_error(&tree));
        mpack_tree_destroy(&tree);
        return false;
    }

    mpack_node_t root = mpack_tree_root(&tree);

    // Version check
    uint16_t version = 1;
    mpack_node_t vNode = mpack_node_map_cstr_optional(root, "v");
    if (!mpack_node_is_missing(vNode))
    {
        version = mpack_node_u16(vNode);
        if (version > FRAMEANIM_MSGPACK_VERSION)
        {
            DEKI_LOG_ERROR("FrameAnimationMsgPackHelper - unsupported version: %d", version);
            mpack_tree_destroy(&tree);
            return false;
        }
    }

    // Spritesheet GUID
    mpack_node_t sNode = mpack_node_map_cstr_optional(root, "s");
    if (!mpack_node_is_missing(sNode))
        out_data->spritesheet_guid = std::string(mpack_node_str(sNode), mpack_node_strlen(sNode));

    // Version 2: multiple animations
    mpack_node_t aNode = mpack_node_map_cstr_optional(root, "a");
    if (version >= 2 && !mpack_node_is_missing(aNode))
    {
        size_t animCount = mpack_node_array_length(aNode);
        for (size_t i = 0; i < animCount; i++)
        {
            mpack_node_t anim_obj = mpack_node_array_at(aNode, i);
            FrameAnimSequence seq;
            seq.loop = true;

            mpack_node_t nNode = mpack_node_map_cstr_optional(anim_obj, "n");
            if (!mpack_node_is_missing(nNode))
                seq.name = std::string(mpack_node_str(nNode), mpack_node_strlen(nNode));

            mpack_node_t lNode = mpack_node_map_cstr_optional(anim_obj, "l");
            if (!mpack_node_is_missing(lNode))
                seq.loop = mpack_node_bool(lNode);

            mpack_node_t fNode = mpack_node_map_cstr_optional(anim_obj, "f");
            if (!mpack_node_is_missing(fNode))
            {
                size_t frameCount = mpack_node_array_length(fNode);
                for (size_t fi = 0; fi < frameCount; fi++)
                {
                    mpack_node_t frame_obj = mpack_node_array_at(fNode, fi);
                    FrameAnimFrame frame;
                    frame.duration = 100;

                    mpack_node_t gNode = mpack_node_map_cstr_optional(frame_obj, "g");
                    if (!mpack_node_is_missing(gNode))
                        frame.frame_guid = std::string(mpack_node_str(gNode), mpack_node_strlen(gNode));

                    mpack_node_t dNode = mpack_node_map_cstr_optional(frame_obj, "d");
                    if (!mpack_node_is_missing(dNode))
                        frame.duration = mpack_node_i32(dNode);

                    seq.frames.push_back(frame);
                }
            }

            out_data->animations.push_back(seq);
        }
    }
    // Version 1 backwards compatibility: single animation
    else
    {
        mpack_node_t fNode = mpack_node_map_cstr_optional(root, "f");
        if (!mpack_node_is_missing(fNode))
        {
            FrameAnimSequence seq;
            seq.loop = true;

            mpack_node_t nNode = mpack_node_map_cstr_optional(root, "n");
            if (!mpack_node_is_missing(nNode))
                seq.name = std::string(mpack_node_str(nNode), mpack_node_strlen(nNode));

            mpack_node_t lNode = mpack_node_map_cstr_optional(root, "l");
            if (!mpack_node_is_missing(lNode))
                seq.loop = mpack_node_bool(lNode);

            size_t frameCount = mpack_node_array_length(fNode);
            for (size_t fi = 0; fi < frameCount; fi++)
            {
                mpack_node_t frame_obj = mpack_node_array_at(fNode, fi);
                FrameAnimFrame frame;
                frame.duration = 100;

                mpack_node_t gNode = mpack_node_map_cstr_optional(frame_obj, "g");
                if (!mpack_node_is_missing(gNode))
                    frame.frame_guid = std::string(mpack_node_str(gNode), mpack_node_strlen(gNode));

                mpack_node_t dNode = mpack_node_map_cstr_optional(frame_obj, "d");
                if (!mpack_node_is_missing(dNode))
                    frame.duration = mpack_node_i32(dNode);

                seq.frames.push_back(frame);
            }

            out_data->animations.push_back(seq);
        }
    }

    mpack_tree_destroy(&tree);
#endif

    int totalFrames = 0;
    for (const auto& anim : out_data->animations)
        totalFrames += static_cast<int>(anim.frames.size());

    DEKI_LOG_DEBUG("FrameAnimationMsgPackHelper::LoadAnimation - loaded %d animations with %d total frames",
                  static_cast<int>(out_data->animations.size()), totalFrames);

    return true;
}

#ifdef DEKI_EDITOR
bool FrameAnimationMsgPackHelper::SaveAnimation(const char* msgpack_path, const FrameAnimationData* anim_data)
{
    if (!msgpack_path || !anim_data)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::SaveAnimation - null parameters");
        return false;
    }

    try
    {
        // Build JSON structure (version 2 format)
        json j;
        j["v"] = static_cast<uint16_t>(FRAMEANIM_MSGPACK_VERSION);
        j["s"] = anim_data->spritesheet_guid;

        // Animations array
        json anims_arr = json::array();
        for (const auto& seq : anim_data->animations)
        {
            json anim_obj;
            anim_obj["n"] = seq.name;
            anim_obj["l"] = seq.loop;

            json frames_arr = json::array();
            for (const auto& frame : seq.frames)
            {
                json frame_obj;
                frame_obj["g"] = frame.frame_guid;
                frame_obj["d"] = static_cast<uint16_t>(frame.duration);
                frames_arr.push_back(frame_obj);
            }
            anim_obj["f"] = frames_arr;

            anims_arr.push_back(anim_obj);
        }
        j["a"] = anims_arr;

        // Convert to MessagePack
        std::vector<uint8_t> msgpack_data = json::to_msgpack(j);

        // Write to file
        std::ofstream file(msgpack_path, std::ios::binary);
        if (!file.is_open())
        {
            DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::SaveAnimation - failed to open for writing: %s", msgpack_path);
            return false;
        }

        file.write(reinterpret_cast<const char*>(msgpack_data.data()), msgpack_data.size());

        int totalFrames = 0;
        for (const auto& seq : anim_data->animations)
            totalFrames += static_cast<int>(seq.frames.size());

        DEKI_LOG_DEBUG("FrameAnimationMsgPackHelper::SaveAnimation - saved %d animations with %d total frames (%zu bytes)",
                      static_cast<int>(anim_data->animations.size()), totalFrames, msgpack_data.size());

        return file.good();
    }
    catch (const std::exception& e)
    {
        DEKI_LOG_ERROR("FrameAnimationMsgPackHelper::SaveAnimation - error: %s", e.what());
        return false;
    }
}
#endif

// Self-register animation loader with AssetManager
namespace {
    struct _AnimLoaderReg {
        _AnimLoaderReg() {
            auto loader = [](const char* p) -> void* {
                auto* data = new FrameAnimationData();
                if (FrameAnimationMsgPackHelper::LoadAnimation(p, data))
                    return data;
                delete data;
                return nullptr;
            };
            auto unloader = [](void* a) { delete static_cast<FrameAnimationData*>(a); };
            Deki::AssetManager::RegisterLoader("FrameAnimationData", loader, unloader);
            Deki::AssetManager::RegisterLoader("Animation", loader, unloader);
        }
    };
    static _AnimLoaderReg s_animLoaderReg;
}
