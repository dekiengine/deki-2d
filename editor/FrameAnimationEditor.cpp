// Asset type editor for frame animations: compiles .anim JSON files to
// MessagePack. Compiled into the editor only, not the runtime.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/AssetTypeRegistry.h>
#include "../FrameAnimationData.h"
#include "../FrameAnimationMsgPack.h"
#include <deki/reflection/Serialization.h>
#include "generated/FrameAnimationData.gen.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace DekiEditor
{

namespace
{
// Compiles a .anim JSON source to its MessagePack cache. When the cache
// already exists, returns Cached without recompiling.
AssetCacheResult HandleFrameAnimCache(const AssetCacheContext& ctx)
{
    if (ctx.hasCachedVersion)
    {
        return AssetCacheResult::Cached;
    }

    std::ifstream file(ctx.absolutePath);
    if (!file.is_open())
    {
        return AssetCacheResult::NotCached;
    }

    try
    {
        nlohmann::json j;
        file >> j;
        Deki2D::FrameAnimationData engineData = Deki::Deserialize<Deki2D::FrameAnimationData>(j);

        std::filesystem::path outPath(ctx.cachePath);
        std::filesystem::path parentDir = outPath.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir))
        {
            std::filesystem::create_directories(parentDir);
        }

        if (FrameAnimationMsgPackHelper::SaveAnimation(ctx.cachePath.c_str(), &engineData))
        {
            return AssetCacheResult::Cached;
        }
    }
    catch (const std::exception&)
    {
    }

    return AssetCacheResult::NotCached;
}

struct FrameAnimCacheRegistrar
{
    FrameAnimCacheRegistrar()
    {
        AssetPipeline::OnStarted([](AssetPipeline* p) { p->RegisterCacheHandler(".anim", HandleFrameAnimCache); });
        AssetTypeRegistry::Instance().RegisterCategory(".anim", AssetCategory::Animation);
    }
};
static FrameAnimCacheRegistrar s_FrameAnimCacheRegistrar;
}  // namespace

class FrameAnimationEditor : public AssetTypeEditor
{
public:
    // "Animation" matches Deki2D::FrameAnimationData::kAssetTypeName and the
    // runtime AssetManager loader in FrameAnimationMsgPack.cpp, so editor and
    // runtime lookups agree.
    const char* GetTypeName() const override { return "Animation"; }
    const char* GetDisplayName() const override { return "Animation"; }
    const char* GetExtension() const override { return ".anim"; }

    const char* GetDefaultContent() const override
    {
        return R"({
  "name": "New Animation",
  "spritesheetGuid": "",
  "loop": true,
  "frames": []
})";
    }

    int GetCompileTarget() const override { return 2; }  // Data

    // Not used for Data targets.
    bool Compile(const std::string& jsonData, std::vector<uint8_t>& rgba, int& width, int& height) override
    {
        return false;
    }

    bool CompileToFile(const std::string& jsonData, const std::string& cachePath) override
    {
        std::filesystem::path filePath(cachePath);
        std::filesystem::path parentDir = filePath.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir))
        {
            std::filesystem::create_directories(parentDir);
        }

        nlohmann::json j = nlohmann::json::parse(jsonData);
        Deki2D::FrameAnimationData engineData = Deki::Deserialize<Deki2D::FrameAnimationData>(j);

        return FrameAnimationMsgPackHelper::SaveAnimation(cachePath.c_str(), &engineData);
    }

    bool OnInspectorGUI(std::string& jsonData, const std::string& assetPath, const std::string& assetGuid) override
    {
        // Frame animations are edited in FrameAnimationEditorWindow, not the
        // Inspector.
        return false;
    }
};

REGISTER_EDITOR(FrameAnimationEditor)

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
