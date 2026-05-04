#include "FontSyncHandler.h"

#ifdef DEKI_EDITOR

#include <deki-editor/AssetPipeline.h>
#include <deki-editor/SubAsset.h>
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

// Register baked font variants as sub-assets AND with AssetManager for a given font GUID.
// AssetManager registration mirrors what HandleFontSync used to do per-size; centralizing
// it here lets HandleFontSync run only on source changes (OnSourceChange policy) instead
// of every project open.
static void RegisterFontSubAssets(const std::string& fontGuid, const std::string& projectPath)
{
    namespace fs = std::filesystem;
    std::string cachePath = projectPath + "/cache";
    std::vector<DekiEditor::SubAssetInfo> subAssets;
    int index = 0;

    for (int sz = 8; sz <= 128; sz++)
    {
        std::string seed = fontGuid + ":" + std::to_string(sz);
        std::string vGuid = Deki::GenerateDeterministicGuid(seed);
        std::string vPath = cachePath + "/" + vGuid;

        if (!fs::exists(vPath)) continue;

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
            pipeline->RegisterSubAssets(fontGuid, subAssets);
    }
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
    std::string hintingStr = settings.value("hinting", std::string("light"));
    FontCompiler::HintingMode hinting = FontCompiler::HintingMode::Light;
    if (hintingStr == "none")        hinting = FontCompiler::HintingMode::None;
    else if (hintingStr == "normal") hinting = FontCompiler::HintingMode::Normal;
    else if (hintingStr == "mono")   hinting = FontCompiler::HintingMode::Mono;

    int oversample = settings.value("oversample", 2);
    if (oversample < 1) oversample = 1;
    if (oversample > 4) oversample = 4;

    std::string decorationStr = settings.value("decoration", std::string("none"));
    FontCompiler::DecorationMode decoration = FontCompiler::DecorationMode::None;
    if (decorationStr == "outline")      decoration = FontCompiler::DecorationMode::Outline;
    else if (decorationStr == "shadow")  decoration = FontCompiler::DecorationMode::Shadow;
    int outlineSize = settings.value("outlineSize", 1);
    if (outlineSize < 1) outlineSize = 1;
    if (outlineSize > 3) outlineSize = 3;
    int shadowDx = settings.value("shadowDx", 1);
    if (shadowDx < -3) shadowDx = -3;
    if (shadowDx > 3) shadowDx = 3;
    int shadowDy = settings.value("shadowDy", 1);
    if (shadowDy < -3) shadowDy = -3;
    if (shadowDy > 3) shadowDy = 3;
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
        // Only skip if BOTH dfont and atlas exist - if atlas is missing, re-bake.
        // AssetManager registration happens centrally in RegisterFontSubAssets via
        // OnImportComplete, so we don't need to register here.
        if (fs::exists(dfontPath) && fs::exists(atlasPath))
        {
            DEKI_LOG_EDITOR("FontSync: Already cached: %s", variantGuid.c_str());
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

        // Write atlas (filename is atlasGuid, no extension)
        // atlasPath already defined above
        if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, result.atlasRGBA.data(),
            result.atlasWidth, result.atlasHeight, DekiEditor::TextureFormat::ALPHA8))
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

        // AssetManager registration happens centrally in RegisterFontSubAssets via
        // OnImportComplete (after ImportAllAssets). No per-size RegisterGuid needed here.
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

    // Sub-asset registration for Asset Browser display happens in the
    // OnImportComplete callback below (see RegisterFontSyncHandlers). Doing it
    // here on every cold-changed path is redundant.
}

// =========================================================================
// BDF Font Sync
// =========================================================================

static void RegisterBdfSubAssets(const std::string& fontGuid, const std::string& projectPath)
{
    std::string cachePath = projectPath + "/cache";
    std::vector<DekiEditor::SubAssetInfo> subAssets;

    std::string vGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf");
    std::string vPath = cachePath + "/" + vGuid;

    if (!fs::exists(vPath)) return;

    // Variant lives in a separate cache file. Centralized here so HandleBdfSync
    // can run only on source changes (OnSourceChange policy).
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
        pipeline->RegisterSubAssets(fontGuid, subAssets);
}

static void HandleBdfSync(
    const std::string& absolutePath,
    const std::string& fontGuid,
    const std::string& projectPath)
{
    DEKI_LOG_EDITOR("BdfSync: HandleBdfSync called for %s (guid=%s)", absolutePath.c_str(), fontGuid.c_str());

    // Read .data sidecar
    std::string dataPath = absolutePath + ".data";
    json j;
    bool hasData = false;

    if (fs::exists(dataPath))
    {
        std::ifstream file(dataPath);
        if (file.is_open())
        {
            try { j = json::parse(file); hasData = true; }
            catch (...) {}
        }
    }

    // Read selected chars from .data, or auto-select all if missing
    std::vector<int> selectedChars;
    if (hasData && j.contains("bdfSettings") && j["bdfSettings"].contains("selectedChars"))
    {
        for (const auto& val : j["bdfSettings"]["selectedChars"])
        {
            if (val.is_number_integer())
                selectedChars.push_back(val.get<int>());
        }
    }

    // Read decoration settings (same schema as TTF's fontSettings — decoration/outlineSize/shadowDx/shadowDy)
    FontCompiler::DecorationMode bdfDecoration = FontCompiler::DecorationMode::None;
    int bdfOutlineSize = 1, bdfShadowDx = 1, bdfShadowDy = 1;
    if (hasData && j.contains("bdfSettings"))
    {
        const auto& settings = j["bdfSettings"];
        std::string dec = settings.value("decoration", std::string("none"));
        if (dec == "outline")      bdfDecoration = FontCompiler::DecorationMode::Outline;
        else if (dec == "shadow")  bdfDecoration = FontCompiler::DecorationMode::Shadow;
        bdfOutlineSize = settings.value("outlineSize", 1);
        if (bdfOutlineSize < 1) bdfOutlineSize = 1;
        if (bdfOutlineSize > 3) bdfOutlineSize = 3;
        bdfShadowDx = settings.value("shadowDx", 1);
        if (bdfShadowDx < -3) bdfShadowDx = -3;
        if (bdfShadowDx > 3) bdfShadowDx = 3;
        bdfShadowDy = settings.value("shadowDy", 1);
        if (bdfShadowDy < -3) bdfShadowDy = -3;
        if (bdfShadowDy > 3) bdfShadowDy = 3;
    }

    // No settings yet — auto-select all codepoints from the BDF file
    if (selectedChars.empty())
    {
        selectedChars = FontCompiler::GetBdfCodepoints(absolutePath);
        if (selectedChars.empty())
        {
            DEKI_LOG_EDITOR("BdfSync: No glyphs found in BDF file");
            return;
        }
        DEKI_LOG_EDITOR("BdfSync: Auto-selecting all %zu chars for %s", selectedChars.size(), absolutePath.c_str());

        // Save default settings to .data so future syncs use them
        std::sort(selectedChars.begin(), selectedChars.end());
        j["bdfSettings"]["selectedChars"] = selectedChars;
        std::ofstream outFile(dataPath);
        if (outFile.is_open())
            outFile << j.dump(2);
    }

    // Deterministic GUIDs
    std::string variantGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf");
    std::string atlasGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf:atlas");

    // Update variants in .data
    if (!j.contains("variants"))
        j["variants"] = json::object();
    j["variants"]["bdf"] = { {"guid", variantGuid}, {"atlasGuid", atlasGuid} };

    std::string cacheDir = projectPath + "/cache";
    fs::create_directories(cacheDir);

    std::string dfontPath = cacheDir + "/" + variantGuid;
    std::string atlasPath = cacheDir + "/" + atlasGuid;

    // Skip if already cached. AssetManager registration + sub-asset registration
    // happen centrally in RegisterBdfSubAssets via OnImportComplete.
    if (fs::exists(dfontPath) && fs::exists(atlasPath))
    {
        DEKI_LOG_EDITOR("BdfSync: Already cached: %s", variantGuid.c_str());
        return;
    }

    // Delete stale dfont if atlas missing
    if (fs::exists(dfontPath) && !fs::exists(atlasPath))
        fs::remove(dfontPath);

    DEKI_LOG_EDITOR("BdfSync: Baking BDF font %s", absolutePath.c_str());

    // Compile
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

    // Write atlas
    if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, result.atlasRGBA.data(),
        result.atlasWidth, result.atlasHeight, DekiEditor::TextureFormat::ALPHA8))
    {
        DEKI_LOG_WARNING("BdfSync: Failed to write atlas %s", atlasPath.c_str());
        return;
    }

    // Write dfont
    if (!FontCompiler::WriteDfontFile(dfontPath, result, atlasGuid))
    {
        DEKI_LOG_WARNING("BdfSync: Failed to write dfont %s", dfontPath.c_str());
        return;
    }

    DEKI_LOG_EDITOR("BdfSync: Baked -> font=%s, atlas=%s", variantGuid.c_str(), atlasGuid.c_str());

    // Save .data with variant GUIDs
    std::ofstream outFile(dataPath);
    if (outFile.is_open())
        outFile << j.dump(2);

    // AssetManager registration + sub-asset registration happen centrally in
    // RegisterBdfSubAssets via OnImportComplete (called after ImportAllAssets).
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
        pipeline->RegisterSyncHandler(".bdf", HandleBdfSync);

        // .dfont is pre-compiled at the source path — the engine's pipeline
        // dispatches a PreCached cache handler so the source IS treated as the
        // cache (no separate cache file, no staleness check). This keeps
        // .dfont knowledge out of AssetPipeline.cpp.
        pipeline->RegisterCacheHandler(".dfont", [](const DekiEditor::AssetCacheContext&) {
            return DekiEditor::AssetCacheResult::PreCached;
        });

        // Cache-variant providers — tell the pipeline which derived cache GUIDs
        // belong to a TTF/OTF/BDF source. Without this, cache cleanup would
        // delete baked font variants as orphans.
        auto ttfProvider = [](const DekiEditor::AssetInfo& info,
                              std::unordered_set<std::string>& valid) {
            // Match the size range that BakeFont actually emits — see
            // FontCompiler / RuntimeFontCache for the canonical size enumeration.
            for (int sz = 8; sz <= 128; ++sz)
            {
                std::string sizeKey = std::to_string(sz);
                valid.insert(Deki::GenerateDeterministicGuid(info.guid + ":" + sizeKey));
                valid.insert(Deki::GenerateDeterministicGuid(info.guid + ":" + sizeKey + ":atlas"));
            }
        };
        pipeline->RegisterCacheVariantProvider(".ttf", ttfProvider);
        pipeline->RegisterCacheVariantProvider(".otf", ttfProvider);
        pipeline->RegisterCacheVariantProvider(".bdf",
            [](const DekiEditor::AssetInfo& info,
               std::unordered_set<std::string>& valid) {
                valid.insert(Deki::GenerateDeterministicGuid(info.guid + ":bdf"));
                valid.insert(Deki::GenerateDeterministicGuid(info.guid + ":bdf:atlas"));
            });

        DEKI_LOG_EDITOR("FontSync: Handlers registered for .ttf, .otf, .bdf, .dfont");
    });

    // After all assets are imported, scan for existing baked font variants
    // and register them as sub-assets (for warm cache / project re-open)
    DekiEditor::AssetPipeline::OnImportComplete([](DekiEditor::AssetPipeline* pipeline) {
        const auto& allAssets = pipeline->GetAllAssets();
        for (const auto& [path, info] : allAssets)
        {
            if (info.guid.empty()) continue;
            std::string ext = fs::path(path).extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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
