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
#include "../FrameAnimationData.h"
#include "../FrameAnimationMsgPack.h"
#include "reflection/Serialization.h"
#include "generated/FrameAnimationData.gen.h"
#include <nlohmann/json.hpp>
#include <filesystem>

namespace DekiEditor
{

class FrameAnimationEditor : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override { return "FrameAnimation"; }
    const char* GetDisplayName() const override { return "Frame Animation"; }
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
