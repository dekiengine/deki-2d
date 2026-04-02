#include "FontSyncHandler.h"

#ifdef DEKI_EDITOR

#include <deki-editor/AssetPipeline.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/AssetData.h>  // For DekiEditor::GenerateGuid
#include "FontCompiler.h"
#include "DekiLogSystem.h"
#include "Guid.h"  // For Deki::GenerateDeterministicGuid
#include "assets/AssetManager.h"  // For registering variant GUIDs
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Deki2D
{

/**
 * @brief Get or create variant GUIDs using deterministic generation
 * GUIDs are always regenerated to ensure consistency with CleanCache
 */
static bool GetOrCreateVariantGuids(const std::string& fontGuid, int fontSize,
                                     std::string& outVariantGuid, std::string& outAtlasGuid,
                                     json& j)
{
    std::string sizeKey = std::to_string(fontSize);

    // Ensure variants object exists
    if (!j.contains("variants"))
        j["variants"] = json::object();

    // Ensure size entry exists
    if (!j["variants"].contains(sizeKey))
        j["variants"][sizeKey] = json::object();

    // Always use deterministic GUIDs based on fontGuid:size
    outVariantGuid = Deki::GenerateDeterministicGuid(fontGuid + ":" + sizeKey);
    j["variants"][sizeKey]["guid"] = outVariantGuid;

    // Atlas GUID: fontGuid:size:atlas
    outAtlasGuid = Deki::GenerateDeterministicGuid(fontGuid + ":" + sizeKey + ":atlas");
    j["variants"][sizeKey]["atlasGuid"] = outAtlasGuid;

    return true;
}

/**
 * @brief Handle font sync - auto-bake configured font sizes
 */
static void HandleFontSync(
    const std::string& absolutePath,
    const std::string& fontGuid,
    const std::string& projectPath)
{
    DEKI_LOG_EDITOR("FontSync: HandleFontSync called for %s (guid=%s, projectPath=%s)",
        absolutePath.c_str(), fontGuid.c_str(), projectPath.c_str());

    // Read .data sidecar for configured sizes
    std::string dataPath = absolutePath + ".data";
    DEKI_LOG_EDITOR("FontSync: Checking .data file at %s", dataPath.c_str());
    if (!fs::exists(dataPath))
    {
        DEKI_LOG_EDITOR("FontSync: No .data file found at %s", dataPath.c_str());
        return;
    }
    DEKI_LOG_EDITOR("FontSync: .data file exists");

    std::ifstream file(dataPath);
    if (!file.is_open())
    {
        DEKI_LOG_WARNING("FontSync: Could not open .data file %s", dataPath.c_str());
        return;
    }

    json j;
    try
    {
        j = json::parse(file);
    }
    catch (const std::exception& e)
    {
        DEKI_LOG_WARNING("FontSync: Failed to parse .data file: %s", e.what());
        return;
    }
    DEKI_LOG_EDITOR("FontSync: Parsed .data file successfully");

    if (!j.contains("fontSettings"))
    {
        DEKI_LOG_EDITOR("FontSync: No fontSettings in .data file");
        return;
    }

    if (!j["fontSettings"].contains("sizes"))
    {
        DEKI_LOG_EDITOR("FontSync: No sizes array in fontSettings");
        return;
    }

    auto& settings = j["fontSettings"];
    int firstChar = settings.value("firstChar", 32);
    int lastChar = settings.value("lastChar", 126);
    DEKI_LOG_EDITOR("FontSync: Found %zu sizes configured", settings["sizes"].size());

    std::string cacheDir = projectPath + "/cache";
    fs::create_directories(cacheDir);

    bool dataFileModified = false;

    for (const auto& sizeVal : settings["sizes"])
    {
        if (!sizeVal.is_number_integer())
            continue;

        int fontSize = sizeVal.get<int>();

        // Get or create variant GUIDs
        std::string sizeKey = std::to_string(fontSize);
        bool guidsExistedBefore = j.contains("variants") &&
                                   j["variants"].contains(sizeKey) &&
                                   j["variants"][sizeKey].contains("guid") &&
                                   j["variants"][sizeKey].contains("atlasGuid");

        std::string variantGuid, atlasGuid;
        if (!GetOrCreateVariantGuids(fontGuid, fontSize, variantGuid, atlasGuid, j))
        {
            DEKI_LOG_WARNING("FontSync: Failed to get/create GUIDs for %s @ %d px", absolutePath.c_str(), fontSize);
            continue;
        }

        // If GUIDs didn't exist before, we created new ones
        if (!guidsExistedBefore)
        {
            dataFileModified = true;
        }

        // Cache file uses GUID as filename (no extension)
        std::string dfontPath = cacheDir + "/" + variantGuid;
        std::string atlasPath = cacheDir + "/" + atlasGuid;

        DEKI_LOG_EDITOR("FontSync: Checking cache at %s", dfontPath.c_str());
        // Only skip if BOTH dfont and atlas exist - if atlas is missing, re-bake
        if (fs::exists(dfontPath) && fs::exists(atlasPath))
        {
            // Register with just the GUID - AssetManager prepends cache dir
            Deki::AssetManager::Get()->RegisterGuid(variantGuid, variantGuid);
            DEKI_LOG_EDITOR("FontSync: Already cached, registered GUID %s", variantGuid.c_str());
            continue;
        }

        // Delete stale dfont file if atlas is missing (will re-bake below)
        if (fs::exists(dfontPath) && !fs::exists(atlasPath))
        {
            DEKI_LOG_EDITOR("FontSync: Deleting stale dfont (atlas missing): %s", dfontPath.c_str());
            fs::remove(dfontPath);
        }

        DEKI_LOG_EDITOR("FontSync: Auto-baking %s @ %d px", absolutePath.c_str(), fontSize);

        // Bake the variant
        FontCompiler::CompileOptions options;
        options.fontSize = fontSize;
        options.firstChar = firstChar;
        options.lastChar = lastChar;
        options.padding = 2;
        options.maxAtlasSize = 2048;
        FontCompiler::CompileResult result;
        if (!FontCompiler::CompileTrueTypeFont(absolutePath, options, result))
        {
            DEKI_LOG_WARNING("FontSync: Failed to compile %s @ %d px", absolutePath.c_str(), fontSize);
            continue;
        }

        // Write atlas (filename is atlasGuid, no extension)
        // atlasPath already defined above
        if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, result.atlasRGBA.data(),
            result.atlasWidth, result.atlasHeight, DekiEditor::TextureFormat::RGB565A8))
        {
            DEKI_LOG_WARNING("FontSync: Failed to write atlas %s", atlasPath.c_str());
            continue;
        }

        // Write DFONT file (filename is variantGuid, no extension)
        // Atlas filename stored inside dfont is just the atlasGuid (no extension)
        if (!FontCompiler::WriteDfontFile(dfontPath, result, atlasGuid))
        {
            DEKI_LOG_WARNING("FontSync: Failed to write dfont %s", dfontPath.c_str());
            continue;
        }

        // Register the variant GUID with AssetManager
        // Use just the GUID - AssetManager prepends cache dir
        Deki::AssetManager::Get()->RegisterGuid(variantGuid, variantGuid);
        DEKI_LOG_EDITOR("FontSync: Registered variant GUID %s", variantGuid.c_str());

        DEKI_LOG_EDITOR("FontSync: Successfully baked %s @ %d px -> font=%s, atlas=%s",
            absolutePath.c_str(), fontSize, variantGuid.c_str(), atlasGuid.c_str());

        dataFileModified = true;
    }

    // Save .data file if we generated new GUIDs
    if (dataFileModified)
    {
        std::ofstream outFile(dataPath);
        if (outFile.is_open())
        {
            outFile << j.dump(2);
            DEKI_LOG_EDITOR("FontSync: Updated .data file with new GUIDs: %s", dataPath.c_str());
        }
    }
}

// Static flag to prevent adding duplicate callbacks
static bool s_FontSyncRegistrationSetup = false;

void RegisterFontSyncHandlers()
{
    DEKI_LOG_EDITOR("FontSync: RegisterFontSyncHandlers() called, setup=%d", s_FontSyncRegistrationSetup ? 1 : 0);

    // Only add the callback once - it will be invoked every time a pipeline starts
    // (because AssetPipeline::Stop() clears m_SyncHandlers)
    if (s_FontSyncRegistrationSetup)
    {
        DEKI_LOG_EDITOR("FontSync: Already registered, skipping");
        return;
    }
    s_FontSyncRegistrationSetup = true;

    DEKI_LOG_EDITOR("FontSync: Adding OnStarted callback");
    DekiEditor::AssetPipeline::OnStarted([](DekiEditor::AssetPipeline* pipeline) {
        DEKI_LOG_EDITOR("FontSync: OnStarted callback invoked, registering handlers");
        pipeline->RegisterSyncHandler(".ttf", HandleFontSync);
        pipeline->RegisterSyncHandler(".otf", HandleFontSync);
        DEKI_LOG_EDITOR("FontSync: Handlers registered for .ttf and .otf");
    });
}

void EnsureFontSizeBaked(const std::string& sourceGuid, int fontSize)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline)
        return;

    const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
    if (!fontInfo)
    {
        DEKI_LOG_WARNING("FontSync: EnsureFontSizeBaked: Cannot find font asset for GUID %s", sourceGuid.c_str());
        return;
    }

    std::string fontPath = (fs::path(pipeline->GetProjectPath()) / fontInfo->path).string();
    std::string dataPath = fontPath + ".data";

    // Load or create .data file
    json j;
    if (fs::exists(dataPath))
    {
        std::ifstream inFile(dataPath);
        if (inFile.is_open())
        {
            try
            {
                j = json::parse(inFile);
            }
            catch (...)
            {
                j = json::object();
            }
        }
    }

    // Ensure fontSettings structure
    if (!j.contains("fontSettings"))
        j["fontSettings"] = json::object();
    if (!j["fontSettings"].contains("sizes"))
        j["fontSettings"]["sizes"] = json::array();
    if (!j["fontSettings"].contains("firstChar"))
        j["fontSettings"]["firstChar"] = 32;
    if (!j["fontSettings"].contains("lastChar"))
        j["fontSettings"]["lastChar"] = 126;

    // Add size if not present
    auto& sizes = j["fontSettings"]["sizes"];
    bool found = false;
    for (const auto& s : sizes)
    {
        if (s.is_number_integer() && s.get<int>() == fontSize)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        sizes.push_back(fontSize);
        std::vector<int> sizeVec;
        for (const auto& s : sizes)
        {
            if (s.is_number_integer())
                sizeVec.push_back(s.get<int>());
        }
        std::sort(sizeVec.begin(), sizeVec.end());
        sizes = sizeVec;
    }

    // Save .data file (only fontSettings - HandleFontSync manages variants)
    std::ofstream outFile(dataPath);
    if (outFile.is_open())
    {
        outFile << j.dump(2);
        outFile.close();
    }

    // Trigger HandleFontSync via RefreshAsset
    pipeline->RefreshAsset(fontInfo->path);

    DEKI_LOG_EDITOR("FontSync: EnsureFontSizeBaked completed for %s @ %d px", sourceGuid.c_str(), fontSize);
}

} // namespace Deki2D

#endif // DEKI_EDITOR
