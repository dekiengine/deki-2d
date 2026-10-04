#include "FontSyncHandler.h"

#ifdef DEKI_EDITOR

#include <deki-editor/AssetPipeline.h>
#include <deki-editor/Paths.h>
#include <deki-editor/SubAsset.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/AssetData.h>  // For DekiEditor::GenerateGuid
#include "FontCompiler.h"
#include <deki/LogSystem.h>
#include <deki/Guid.h>                 // For Deki::GenerateDeterministicGuid
#include <deki/assets/AssetManager.h>  // For registering variant GUIDs
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Deki2D
{

// The deterministic variant and atlas GUIDs for one size. They are always
// regenerated, so they match what CleanCache expects.
static bool GetOrCreateVariantGuids(const std::string& fontGuid, int fontSize, std::string& outVariantGuid,
                                    std::string& outAtlasGuid, json& j)
{
    std::string sizeKey = std::to_string(fontSize);

    if (!j.contains("variants"))
    {
        j["variants"] = json::object();
    }

    if (!j["variants"].contains(sizeKey))
    {
        j["variants"][sizeKey] = json::object();
    }

    // Deterministic, from "fontGuid:size".
    outVariantGuid = Deki::GenerateDeterministicGuid(fontGuid + ":" + sizeKey);
    j["variants"][sizeKey]["guid"] = outVariantGuid;

    // The atlas: "fontGuid:size:atlas".
    outAtlasGuid = Deki::GenerateDeterministicGuid(fontGuid + ":" + sizeKey + ":atlas");
    j["variants"][sizeKey]["atlasGuid"] = outAtlasGuid;

    return true;
}

// Registers a font's baked variants as sub-assets and with AssetManager.
// Doing it here lets HandleFontSync run only when the source changes
// (OnSourceChange policy) instead of on every project open.
static void RegisterFontSubAssets(const std::string& fontGuid, const std::string& projectPath)
{
    namespace fs = std::filesystem;
    // The editor defines where the cache lives; a package must not hardcode it.
    std::string cachePath = DekiEditor::GetCacheDirectory(projectPath);
    std::vector<DekiEditor::SubAssetInfo> subAssets;
    int index = 0;

    for (int sz = 8; sz <= 128; sz++)
    {
        std::string seed = fontGuid + ":" + std::to_string(sz);
        std::string vGuid = Deki::GenerateDeterministicGuid(seed);
        std::string vPath = cachePath + "/" + vGuid;

        if (!fs::exists(vPath))
        {
            continue;
        }

        // Variant lives in a separate cache file (filename == GUID).
        Deki::AssetManager::Get()->RegisterGuid(vGuid, vGuid);

        DekiEditor::SubAssetInfo fontSub;
        fontSub.guid = vGuid;
        fontSub.parentGuid = fontGuid;
        fontSub.subAssetIndex = index++;
        fontSub.name = std::to_string(sz) + "px";
        fontSub.depth = 0;
        fontSub.cachePath = vPath;
        subAssets.push_back(fontSub);

        std::string aGuid = Deki::GenerateDeterministicGuid(seed + ":atlas");
        std::string aPath = cachePath + "/" + aGuid;

        if (fs::exists(aPath))
        {
            DekiEditor::SubAssetInfo atlasSub;
            atlasSub.guid = aGuid;
            atlasSub.parentGuid = fontGuid;
            atlasSub.subAssetIndex = index++;
            atlasSub.name = std::to_string(sz) + "px atlas";
            atlasSub.depth = 1;
            atlasSub.cachePath = aPath;
            atlasSub.hasPreview = true;
            subAssets.push_back(atlasSub);
        }
    }

    if (!subAssets.empty())
    {
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        if (pipeline)
        {
            pipeline->RegisterSubAssets(fontGuid, subAssets);
        }
    }
}

// Font sync: bakes every size the font's settings list.
static void HandleFontSync(const std::string& absolutePath, const std::string& fontGuid, const std::string& projectPath)
{
    // The configured sizes, from the .data sidecar.
    std::string dataPath = absolutePath + ".data";
    if (!fs::exists(dataPath))
    {
        DEKI_LOG_DEBUG("FontSync: No .data file found at %s", dataPath.c_str());
        return;
    }

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

    if (!j.contains("fontSettings"))
    {
        DEKI_LOG_DEBUG("FontSync: No fontSettings in .data file");
        return;
    }

    if (!j["fontSettings"].contains("sizes"))
    {
        DEKI_LOG_DEBUG("FontSync: No sizes array in fontSettings");
        return;
    }

    auto& settings = j["fontSettings"];
    int firstChar = settings.value("firstChar", 32);
    int lastChar = settings.value("lastChar", 126);
    std::string hintingStr = settings.value("hinting", std::string("light"));
    FontCompiler::HintingMode hinting = FontCompiler::HintingMode::Light;
    if (hintingStr == "none")
    {
        hinting = FontCompiler::HintingMode::None;
    }
    else if (hintingStr == "normal")
    {
        hinting = FontCompiler::HintingMode::Normal;
    }
    else if (hintingStr == "mono")
    {
        hinting = FontCompiler::HintingMode::Mono;
    }

    int oversample = settings.value("oversample", 2);
    if (oversample < 1)
    {
        oversample = 1;
    }
    if (oversample > 4)
    {
        oversample = 4;
    }

    std::string decorationStr = settings.value("decoration", std::string("none"));
    FontCompiler::DecorationMode decoration = FontCompiler::DecorationMode::None;
    if (decorationStr == "outline")
    {
        decoration = FontCompiler::DecorationMode::Outline;
    }
    else if (decorationStr == "shadow")
    {
        decoration = FontCompiler::DecorationMode::Shadow;
    }
    int outlineSize = settings.value("outlineSize", 1);
    if (outlineSize < 1)
    {
        outlineSize = 1;
    }
    if (outlineSize > 3)
    {
        outlineSize = 3;
    }
    int shadowDx = settings.value("shadowDx", 1);
    if (shadowDx < -3)
    {
        shadowDx = -3;
    }
    if (shadowDx > 3)
    {
        shadowDx = 3;
    }
    int shadowDy = settings.value("shadowDy", 1);
    if (shadowDy < -3)
    {
        shadowDy = -3;
    }
    if (shadowDy > 3)
    {
        shadowDy = 3;
    }

    std::string cacheDir = DekiEditor::GetCacheDirectory(projectPath);
    fs::create_directories(cacheDir);

    bool dataFileModified = false;

    for (const auto& sizeVal : settings["sizes"])
    {
        if (!sizeVal.is_number_integer())
        {
            continue;
        }

        int fontSize = sizeVal.get<int>();

        std::string sizeKey = std::to_string(fontSize);
        bool guidsExistedBefore = j.contains("variants") && j["variants"].contains(sizeKey) &&
                                  j["variants"][sizeKey].contains("guid") &&
                                  j["variants"][sizeKey].contains("atlasGuid");

        std::string variantGuid, atlasGuid;
        if (!GetOrCreateVariantGuids(fontGuid, fontSize, variantGuid, atlasGuid, j))
        {
            DEKI_LOG_WARNING("FontSync: Failed to get/create GUIDs for %s @ %d px", absolutePath.c_str(), fontSize);
            continue;
        }

        // New GUIDs were created.
        if (!guidsExistedBefore)
        {
            dataFileModified = true;
        }

        // The cache file is named by the GUID, with no extension.
        std::string dfontPath = cacheDir + "/" + variantGuid;
        std::string atlasPath = cacheDir + "/" + atlasGuid;

        // Skip only when both the dfont and the atlas exist. AssetManager
        // registration happens in RegisterFontSubAssets (OnImportComplete).
        if (fs::exists(dfontPath) && fs::exists(atlasPath))
        {
            continue;
        }

        // The atlas is missing: delete the dfont and bake again below.
        if (fs::exists(dfontPath) && !fs::exists(atlasPath))
        {
            DEKI_LOG_EDITOR("FontSync: Deleting stale dfont (atlas missing): %s", dfontPath.c_str());
            fs::remove(dfontPath);
        }

        FontCompiler::CompileOptions options;
        options.fontSize = fontSize;
        options.firstChar = firstChar;
        options.lastChar = lastChar;
        options.padding = 2;
        options.maxAtlasSize = 2048;
        options.hinting = hinting;
        options.oversample = oversample;
        options.decoration = decoration;
        options.outlineSize = outlineSize;
        options.shadowDx = shadowDx;
        options.shadowDy = shadowDy;
        FontCompiler::CompileResult result;
        if (!FontCompiler::CompileTrueTypeFont(absolutePath, options, result))
        {
            DEKI_LOG_WARNING("FontSync: Failed to compile %s @ %d px", absolutePath.c_str(), fontSize);
            continue;
        }

        // The atlas file is named by atlasGuid, with no extension.
        if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, result.atlasRGBA.data(), result.atlasWidth,
                                                       result.atlasHeight, DekiEditor::TextureFormat::ALPHA8))
        {
            DEKI_LOG_WARNING("FontSync: Failed to write atlas %s", atlasPath.c_str());
            continue;
        }

        // The dfont is named by variantGuid; the atlas name stored inside it is
        // just atlasGuid. Neither has an extension.
        if (!FontCompiler::WriteDfontFile(dfontPath, result, atlasGuid))
        {
            DEKI_LOG_WARNING("FontSync: Failed to write dfont %s", dfontPath.c_str());
            continue;
        }

        // AssetManager registration happens in RegisterFontSubAssets, from
        // OnImportComplete (after ImportAllAssets).
        DEKI_LOG_EDITOR("FontSync: baked %s @ %d px -> font=%s, atlas=%s", absolutePath.c_str(), fontSize,
                        variantGuid.c_str(), atlasGuid.c_str());

        dataFileModified = true;
    }

    // Save the .data file when new GUIDs were made.
    if (dataFileModified)
    {
        std::ofstream outFile(dataPath);
        if (outFile.is_open())
        {
            outFile << j.dump(2);
            DEKI_LOG_DEBUG("FontSync: Updated .data file with new GUIDs: %s", dataPath.c_str());
        }
    }

    // Sub-assets for the Asset Browser are registered in the OnImportComplete
    // callback (see RegisterFontSyncHandlers).
}

// =========================================================================
// BDF Font Sync
// =========================================================================

static void RegisterBdfSubAssets(const std::string& fontGuid, const std::string& projectPath)
{
    // The editor defines where the cache lives; a package must not hardcode it.
    std::string cachePath = DekiEditor::GetCacheDirectory(projectPath);
    std::vector<DekiEditor::SubAssetInfo> subAssets;

    std::string vGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf");
    std::string vPath = cachePath + "/" + vGuid;

    if (!fs::exists(vPath))
    {
        return;
    }

    // The variant has its own cache file. Registered here so HandleBdfSync
    // runs only when the source changes (OnSourceChange policy).
    Deki::AssetManager::Get()->RegisterGuid(vGuid, vGuid);

    DekiEditor::SubAssetInfo fontSub;
    fontSub.guid = vGuid;
    fontSub.parentGuid = fontGuid;
    fontSub.subAssetIndex = 0;
    fontSub.name = "bdf";
    fontSub.depth = 0;
    fontSub.cachePath = vPath;
    subAssets.push_back(fontSub);

    std::string aGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf:atlas");
    std::string aPath = cachePath + "/" + aGuid;
    if (fs::exists(aPath))
    {
        DekiEditor::SubAssetInfo atlasSub;
        atlasSub.guid = aGuid;
        atlasSub.parentGuid = fontGuid;
        atlasSub.subAssetIndex = 1;
        atlasSub.name = "bdf atlas";
        atlasSub.depth = 1;
        atlasSub.cachePath = aPath;
        atlasSub.hasPreview = true;
        subAssets.push_back(atlasSub);
    }

    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (pipeline)
    {
        pipeline->RegisterSubAssets(fontGuid, subAssets);
    }
}

static void HandleBdfSync(const std::string& absolutePath, const std::string& fontGuid, const std::string& projectPath)
{
    std::string dataPath = absolutePath + ".data";
    json j;
    bool hasData = false;

    if (fs::exists(dataPath))
    {
        std::ifstream file(dataPath);
        if (file.is_open())
        {
            try
            {
                j = json::parse(file);
                hasData = true;
            }
            catch (...)
            {
            }
        }
    }

    // The selected characters from .data; all of them when none are listed.
    std::vector<int> selectedChars;
    if (hasData && j.contains("bdfSettings") && j["bdfSettings"].contains("selectedChars"))
    {
        for (const auto& val : j["bdfSettings"]["selectedChars"])
        {
            if (val.is_number_integer())
            {
                selectedChars.push_back(val.get<int>());
            }
        }
    }

    // Decoration settings, in the same form as a TTF's fontSettings
    // (decoration, outlineSize, shadowDx, shadowDy).
    FontCompiler::DecorationMode bdfDecoration = FontCompiler::DecorationMode::None;
    int bdfOutlineSize = 1, bdfShadowDx = 1, bdfShadowDy = 1;
    if (hasData && j.contains("bdfSettings"))
    {
        const auto& settings = j["bdfSettings"];
        std::string dec = settings.value("decoration", std::string("none"));
        if (dec == "outline")
        {
            bdfDecoration = FontCompiler::DecorationMode::Outline;
        }
        else if (dec == "shadow")
        {
            bdfDecoration = FontCompiler::DecorationMode::Shadow;
        }
        bdfOutlineSize = settings.value("outlineSize", 1);
        if (bdfOutlineSize < 1)
        {
            bdfOutlineSize = 1;
        }
        if (bdfOutlineSize > 3)
        {
            bdfOutlineSize = 3;
        }
        bdfShadowDx = settings.value("shadowDx", 1);
        if (bdfShadowDx < -3)
        {
            bdfShadowDx = -3;
        }
        if (bdfShadowDx > 3)
        {
            bdfShadowDx = 3;
        }
        bdfShadowDy = settings.value("shadowDy", 1);
        if (bdfShadowDy < -3)
        {
            bdfShadowDy = -3;
        }
        if (bdfShadowDy > 3)
        {
            bdfShadowDy = 3;
        }
    }

    // No settings yet: select every codepoint in the BDF file.
    if (selectedChars.empty())
    {
        selectedChars = FontCompiler::GetBdfCodepoints(absolutePath);
        if (selectedChars.empty())
        {
            DEKI_LOG_WARNING("BdfSync: No glyphs found in %s", absolutePath.c_str());
            return;
        }
        DEKI_LOG_DEBUG("BdfSync: Auto-selecting all %zu chars for %s", selectedChars.size(), absolutePath.c_str());

        // Save them to .data so later syncs use them.
        std::sort(selectedChars.begin(), selectedChars.end());
        j["bdfSettings"]["selectedChars"] = selectedChars;
        std::ofstream outFile(dataPath);
        if (outFile.is_open())
        {
            outFile << j.dump(2);
        }
    }

    std::string variantGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf");
    std::string atlasGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf:atlas");

    if (!j.contains("variants"))
    {
        j["variants"] = json::object();
    }
    j["variants"]["bdf"] = { { "guid", variantGuid }, { "atlasGuid", atlasGuid } };

    std::string cacheDir = DekiEditor::GetCacheDirectory(projectPath);
    fs::create_directories(cacheDir);

    std::string dfontPath = cacheDir + "/" + variantGuid;
    std::string atlasPath = cacheDir + "/" + atlasGuid;

    // Already cached. AssetManager and sub-asset registration happen in
    // RegisterBdfSubAssets (OnImportComplete).
    if (fs::exists(dfontPath) && fs::exists(atlasPath))
    {
        return;
    }

    // The atlas is missing: delete the dfont and bake again.
    if (fs::exists(dfontPath) && !fs::exists(atlasPath))
    {
        fs::remove(dfontPath);
    }

    FontCompiler::BdfCompileOptions options;
    options.selectedChars = selectedChars;
    options.padding = 2;
    options.maxAtlasSize = 2048;
    options.decoration = bdfDecoration;
    options.outlineSize = bdfOutlineSize;
    options.shadowDx = bdfShadowDx;
    options.shadowDy = bdfShadowDy;

    FontCompiler::CompileResult result;
    if (!FontCompiler::CompileBdfFont(absolutePath, options, result))
    {
        DEKI_LOG_WARNING("BdfSync: Failed to compile %s", absolutePath.c_str());
        return;
    }

    if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, result.atlasRGBA.data(), result.atlasWidth,
                                                   result.atlasHeight, DekiEditor::TextureFormat::ALPHA8))
    {
        DEKI_LOG_WARNING("BdfSync: Failed to write atlas %s", atlasPath.c_str());
        return;
    }

    if (!FontCompiler::WriteDfontFile(dfontPath, result, atlasGuid))
    {
        DEKI_LOG_WARNING("BdfSync: Failed to write dfont %s", dfontPath.c_str());
        return;
    }

    DEKI_LOG_EDITOR("BdfSync: baked %s -> font=%s, atlas=%s", absolutePath.c_str(), variantGuid.c_str(),
                    atlasGuid.c_str());

    // Save .data with the variant GUIDs.
    std::ofstream outFile(dataPath);
    if (outFile.is_open())
    {
        outFile << j.dump(2);
    }

    // AssetManager and sub-asset registration happen in RegisterBdfSubAssets
    // (OnImportComplete, after ImportAllAssets).
}

// Keeps the callback from being added twice.
static bool s_FontSyncRegistrationSetup = false;

void RegisterFontSyncHandlers()
{
    // Added once; it runs every time a pipeline starts, because
    // AssetPipeline::Stop() clears m_SyncHandlers.
    if (s_FontSyncRegistrationSetup)
    {
        return;
    }
    s_FontSyncRegistrationSetup = true;

    DekiEditor::AssetPipeline::OnStarted(
        [](DekiEditor::AssetPipeline* pipeline)
        {
            pipeline->RegisterSyncHandler(".ttf", HandleFontSync);
            pipeline->RegisterSyncHandler(".otf", HandleFontSync);
            pipeline->RegisterSyncHandler(".bdf", HandleBdfSync);

            // A .dfont is already compiled, so its handler returns PreCached:
            // the source is its own cache (no separate file, no staleness
            // check). This keeps .dfont knowledge out of AssetPipeline.cpp.
            pipeline->RegisterCacheHandler(".dfont", [](const DekiEditor::AssetCacheContext&)
                                           { return DekiEditor::AssetCacheResult::PreCached; });

            // Cache-variant providers tell the pipeline which derived cache GUIDs
            // belong to a TTF/OTF/BDF source, so cache cleanup keeps the baked
            // variants.
            auto ttfProvider = [](const DekiEditor::AssetInfo& info, std::unordered_set<std::string>& valid)
            {
                // The size range BakeFont emits (see FontCompiler and
                // RuntimeFontCache).
                for (int sz = 8; sz <= 128; ++sz)
                {
                    std::string sizeKey = std::to_string(sz);
                    valid.insert(Deki::GenerateDeterministicGuid(info.guid + ":" + sizeKey));
                    valid.insert(Deki::GenerateDeterministicGuid(info.guid + ":" + sizeKey + ":atlas"));
                }
            };
            pipeline->RegisterCacheVariantProvider(".ttf", ttfProvider);
            pipeline->RegisterCacheVariantProvider(".otf", ttfProvider);
            pipeline->RegisterCacheVariantProvider(
                ".bdf",
                [](const DekiEditor::AssetInfo& info, std::unordered_set<std::string>& valid)
                {
                    valid.insert(Deki::GenerateDeterministicGuid(info.guid + ":bdf"));
                    valid.insert(Deki::GenerateDeterministicGuid(info.guid + ":bdf:atlas"));
                });
        });

    // After all assets are imported, register the baked font variants already
    // on disk as sub-assets (for a reopened project with a warm cache).
    DekiEditor::AssetPipeline::OnImportComplete(
        [](DekiEditor::AssetPipeline* pipeline)
        {
            const auto& allAssets = pipeline->GetAllAssets();
            for (const auto& [path, info] : allAssets)
            {
                if (info.guid.empty())
                {
                    continue;
                }
                std::string ext = fs::path(path).extension().string();
                for (auto& c : ext)
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (ext == ".ttf" || ext == ".otf")
                {
                    RegisterFontSubAssets(info.guid, pipeline->GetProjectPath());
                }
                else if (ext == ".bdf")
                {
                    RegisterBdfSubAssets(info.guid, pipeline->GetProjectPath());
                }
            }
        });
}

void EnsureFontSizeBaked(const std::string& sourceGuid, int fontSize)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline)
    {
        return;
    }

    const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
    if (!fontInfo)
    {
        DEKI_LOG_WARNING("FontSync: EnsureFontSizeBaked: Cannot find font asset for GUID %s", sourceGuid.c_str());
        return;
    }

    std::string fontPath = (fs::path(pipeline->GetProjectPath()) / fontInfo->path).string();
    std::string dataPath = fontPath + ".data";

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

    if (!j.contains("fontSettings"))
    {
        j["fontSettings"] = json::object();
    }
    if (!j["fontSettings"].contains("sizes"))
    {
        j["fontSettings"]["sizes"] = json::array();
    }
    if (!j["fontSettings"].contains("firstChar"))
    {
        j["fontSettings"]["firstChar"] = 32;
    }
    if (!j["fontSettings"].contains("lastChar"))
    {
        j["fontSettings"]["lastChar"] = 126;
    }

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
            {
                sizeVec.push_back(s.get<int>());
            }
        }
        std::sort(sizeVec.begin(), sizeVec.end());
        sizes = sizeVec;
    }

    // Only fontSettings changes here; HandleFontSync manages the variants.
    std::ofstream outFile(dataPath);
    if (outFile.is_open())
    {
        outFile << j.dump(2);
        outFile.close();
    }

    // Runs HandleFontSync.
    pipeline->RefreshAsset(fontInfo->path);

    // Register what was just baked. Otherwise only the OnImportComplete
    // callback, which runs at project open, would give these GUIDs to
    // AssetManager, and a size baked mid-session could not be loaded.
    // RegisterFontSubAssets can safely run again.
    RegisterFontSubAssets(sourceGuid, pipeline->GetProjectPath());

    DEKI_LOG_DEBUG("FontSync: EnsureFontSizeBaked completed for %s @ %d px", sourceGuid.c_str(), fontSize);
}

}  // namespace Deki2D

#endif  // DEKI_EDITOR
