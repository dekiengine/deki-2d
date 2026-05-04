/**
 * @file FrameAnimationEditor.cpp
 * @brief Asset type editor for frame animations
 *
 * Compiles .frameanim JSON files to binary MessagePack format.
 * This file is only compiled into the editor, not the runtime.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/AssetPipeline.h>
#include "../FrameAnimationData.h"
#include "../FrameAnimationMsgPack.h"
#include "reflection/Serialization.h"
#include "generated/FrameAnimationData.gen.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace DekiEditor
{

namespace
{
// Compile a .frameanim JSON source to its MessagePack cache. Idempotent — if
// the cache already exists, returns Cached without recompiling.
AssetCacheResult HandleFrameAnimCache(const AssetCacheContext& ctx)
{
    if (ctx.hasCachedVersion)
        return AssetCacheResult::Cached;

    std::ifstream file(ctx.absolutePath);
    if (!file.is_open())
        return AssetCacheResult::NotCached;

    try
    {
        nlohmann::json j;
        file >> j;
        FrameAnimationData engineData = Deserialize<FrameAnimationData>(j);

        std::filesystem::path outPath(ctx.cachePath);
        std::filesystem::path parentDir = outPath.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir))
            std::filesystem::create_directories(parentDir);

        if (FrameAnimationMsgPackHelper::SaveAnimation(ctx.cachePath.c_str(), &engineData))
            return AssetCacheResult::Cached;
    }
    catch (const std::exception&) {}

    return AssetCacheResult::NotCached;
}

struct FrameAnimCacheRegistrar
{
    FrameAnimCacheRegistrar()
    {
        AssetPipeline::OnStarted([](AssetPipeline* p) {
            p->RegisterCacheHandler(".frameanim", HandleFrameAnimCache);
        });
    }
};
static FrameAnimCacheRegistrar s_FrameAnimCacheRegistrar;
} // namespace

class FrameAnimationEditor : public AssetTypeEditor
{
public:
    // typeName "Animation" matches FrameAnimationData::AssetTypeName and the
    // runtime AssetManager loader registered in FrameAnimationMsgPack.cpp, so
    // editor-side and runtime-side asset lookups agree.
    const char* GetTypeName() const override { return "Animation"; }
    const char* GetDisplayName() const override { return "Animation"; }
    const char* GetExtension() const override { return ".frameanim"; }

    const char* GetDefaultContent() const override
    {
        return R"({
  "name": "New Animation",
  "spritesheet_guid": "",
  "loop": true,
  "frames": []
})";
    }

    int GetCompileTarget() const override { return 2; }  // Data

    // Not used for Data targets
    bool Compile(const std::string& jsonData,
                 std::vector<uint8_t>& rgba,
                 int& width, int& height) override
    {
        return false;
    }

    bool CompileToFile(const std::string& jsonData,
                       const std::string& cachePath) override
    {
        // Create cache directory if needed
        std::filesystem::path filePath(cachePath);
        std::filesystem::path parentDir = filePath.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir))
        {
            std::filesystem::create_directories(parentDir);
        }

        // Deserialize JSON to engine struct
        nlohmann::json j = nlohmann::json::parse(jsonData);
        FrameAnimationData engineData = Deserialize<FrameAnimationData>(j);

        // Save as MessagePack
        return FrameAnimationMsgPackHelper::SaveAnimation(cachePath.c_str(), &engineData);
    }

    bool OnInspectorGUI(std::string& jsonData,
                        const std::string& assetPath,
                        const std::string& assetGuid) override
    {
        // Frame animation editing is done via the FrameAnimationEditorWindow tool
        // Return false (no modifications via inspector)
        return false;
    }
};

REGISTER_EDITOR(FrameAnimationEditor)

} // namespace DekiEditor

#endif // DEKI_EDITOR
