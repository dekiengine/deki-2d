#include "FontSyncHandler.h"

#ifdef DEKI_EDITOR

#include <deki-editor/AssetPipeline.h>
#include <deki-editor/EditorApplication.h>
#include <deki-editor/Paths.h>
#include <deki-editor/SubAsset.h>
#include <deki-editor/TextureImporter.h>
#include <deki/LogSystem.h>
#include <deki/Guid.h>
#include <deki/assets/AssetManager.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Deki2D
{

namespace
{

// Goes up when baking changes what it writes, so older caches bake again.
constexpr int kBakeVersion = 1;

std::string VariantGuid(const std::string& fontGuid, int fontSize)
{
    return Deki::GenerateDeterministicGuid(fontGuid + ":" + std::to_string(fontSize));
}

std::string AtlasGuid(const std::string& fontGuid, int fontSize)
{
    return Deki::GenerateDeterministicGuid(fontGuid + ":" + std::to_string(fontSize) + ":atlas");
}

std::string BakeKey(const std::string& fontGuid, int fontSize)
{
    return fontGuid + ":" + std::to_string(fontSize);
}

std::string LowerExtension(const std::string& path)
{
    std::string ext = fs::path(path).extension().string();
    for (char& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

// The .data sidecar as JSON, or an empty object when it is missing or does
// not parse.
json ReadDataFile(const std::string& dataPath)
{
    std::ifstream file(dataPath);
    if (!file.is_open())
    {
        return json::object();
    }
    try
    {
        json j = json::parse(file);
        return j.is_object() ? j : json::object();
    }
    catch (const std::exception&)
    {
        return json::object();
    }
}

int ReadClamped(const json& settings, const char* key, int fallback, int lo, int hi)
{
    if (!settings.contains(key) || !settings[key].is_number_integer())
    {
        return fallback;
    }
    return std::clamp(settings[key].get<int>(), lo, hi);
}

FontCompiler::HintingMode HintingFromSetting(const std::string& s)
{
    if (s == "none")
    {
        return FontCompiler::HintingMode::None;
    }
    if (s == "normal")
    {
        return FontCompiler::HintingMode::Normal;
    }
    if (s == "mono")
    {
        return FontCompiler::HintingMode::Mono;
    }
    return FontCompiler::HintingMode::Light;
}

FontCompiler::DecorationMode DecorationFromSetting(const std::string& s)
{
    if (s == "outline")
    {
        return FontCompiler::DecorationMode::Outline;
    }
    if (s == "shadow")
    {
        return FontCompiler::DecorationMode::Shadow;
    }
    return FontCompiler::DecorationMode::None;
}

// The source file's size and time, so a changed font file bakes again.
std::string SourceStamp(const std::string& path)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    const auto time = fs::last_write_time(path, ec).time_since_epoch().count();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%llu:%lld", static_cast<unsigned long long>(ec ? 0 : size),
                  static_cast<long long>(time));
    return buf;
}

// What a TrueType variant baked now is made from: the settings and the source
// file. A cached variant made from another stamp is stale.
std::string MakeTtfStamp(const std::string& fontPath, const FontCompiler::CompileOptions& o)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "ttf%d:%d-%d:p%d:m%d:h%d:o%d:d%d:%d:%d:%d:", kBakeVersion, o.firstChar, o.lastChar,
                  o.padding, o.maxAtlasSize, static_cast<int>(o.hinting), o.oversample, static_cast<int>(o.decoration),
                  o.outlineSize, o.shadowDx, o.shadowDy);
    return buf + SourceStamp(fontPath);
}

// =========================================================================
// Bake stamps
// =========================================================================

// The stamp each font's cached variants were baked from, by font GUID, kept
// in cache/font_bakes.json (the pipeline keeps .json files in the cache).
std::unordered_map<std::string, std::string> s_BakedStamps;
std::string s_BakedStampsProject;

// The stamp a bake of each font made now would have, by font GUID. Worked
// out when first needed and again whenever the font syncs.
std::unordered_map<std::string, std::string> s_CurrentStamps;

std::string BakedStampsPath(const std::string& projectPath)
{
    return (fs::path(DekiEditor::GetCacheDirectory(projectPath)) / "font_bakes.json").string();
}

void LoadBakedStamps(const std::string& projectPath)
{
    if (s_BakedStampsProject == projectPath)
    {
        return;
    }
    s_BakedStampsProject = projectPath;
    s_BakedStamps.clear();
    s_CurrentStamps.clear();

    json j = ReadDataFile(BakedStampsPath(projectPath));
    for (const auto& [guid, stamp] : j.items())
    {
        if (stamp.is_string())
        {
            s_BakedStamps[guid] = stamp.get<std::string>();
        }
    }
}

void SaveBakedStamps(const std::string& projectPath)
{
    json j = json::object();
    for (const auto& [guid, stamp] : s_BakedStamps)
    {
        j[guid] = stamp;
    }
    fs::create_directories(DekiEditor::GetCacheDirectory(projectPath));
    std::ofstream out(BakedStampsPath(projectPath));
    if (out.is_open())
    {
        out << j.dump(2);
    }
}

const std::string& CurrentTtfStamp(const std::string& fontGuid, const std::string& fontPath)
{
    auto it = s_CurrentStamps.find(fontGuid);
    if (it == s_CurrentStamps.end())
    {
        it = s_CurrentStamps.emplace(fontGuid, MakeTtfStamp(fontPath, LoadFontCompileOptions(fontPath))).first;
    }
    return it->second;
}

// =========================================================================
// Background baking
// =========================================================================

struct BakeJob
{
    std::string fontGuid;
    std::string fontPath;
    int fontSize = 0;
    FontCompiler::CompileOptions options;
    std::string stamp;
    const void* requester = nullptr;  // null once two texts want the size
    bool replaces = false;            // a re-bake of a size texts may have loaded
};

struct BakeResult
{
    BakeJob job;
    bool ok = false;
    FontCompiler::CompileResult result;
};

// Guards the queue, the results and what the worker is doing. The worker
// thread is detached; StopFontBaker waits for it to go idle.
std::mutex s_BakeMutex;
std::condition_variable s_BakeIdle;
std::deque<BakeJob> s_BakeQueue;
std::vector<BakeResult> s_BakeResults;
std::string s_RunningKey;  // the bake compiling now, or ""
std::string s_RunningStamp;
bool s_WorkerBusy = false;
bool s_Stopping = false;
std::atomic<bool> s_HaveResults{ false };

// Bakes that failed, as "key:stamp", so a text does not retry every frame.
std::unordered_set<std::string> s_FailedBakes;

void WorkerLoop()
{
    for (;;)
    {
        BakeJob job;
        {
            std::lock_guard<std::mutex> lock(s_BakeMutex);
            if (s_BakeQueue.empty() || s_Stopping)
            {
                s_WorkerBusy = false;
                s_RunningKey.clear();
                s_BakeIdle.notify_all();
                return;
            }
            job = std::move(s_BakeQueue.front());
            s_BakeQueue.pop_front();
            s_RunningKey = BakeKey(job.fontGuid, job.fontSize);
            s_RunningStamp = job.stamp;
        }

        BakeResult done;
        done.job = std::move(job);
        done.job.options.fontSize = done.job.fontSize;
        done.ok = FontCompiler::CompileTrueTypeFont(done.job.fontPath, done.job.options, done.result);

        {
            std::lock_guard<std::mutex> lock(s_BakeMutex);
            s_BakeResults.push_back(std::move(done));
            s_RunningKey.clear();
        }
        s_HaveResults = true;
    }
}

void Enqueue(BakeJob job)
{
    bool startWorker = false;
    {
        std::lock_guard<std::mutex> lock(s_BakeMutex);
        if (s_Stopping)
        {
            return;
        }

        // Jobs that have not started and are no longer wanted: older ones from
        // the same requester, and ones made from the font's old settings.
        bool queued = false;
        for (auto it = s_BakeQueue.begin(); it != s_BakeQueue.end();)
        {
            const bool sameFont = it->fontGuid == job.fontGuid;
            const bool sameKey = sameFont && it->fontSize == job.fontSize;
            if (sameKey && it->stamp == job.stamp)
            {
                if (it->requester != job.requester)
                {
                    it->requester = nullptr;
                }
                it->replaces = it->replaces || job.replaces;
                queued = true;
                ++it;
            }
            else if ((sameFont && it->stamp != job.stamp) ||
                     (job.requester && it->requester == job.requester && !it->replaces))
            {
                it = s_BakeQueue.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (queued)
        {
            return;
        }

        // Compiling now, or compiled and waiting to be written.
        const std::string key = BakeKey(job.fontGuid, job.fontSize);
        if (s_RunningKey == key && s_RunningStamp == job.stamp)
        {
            return;
        }
        for (const auto& done : s_BakeResults)
        {
            if (BakeKey(done.job.fontGuid, done.job.fontSize) == key && done.job.stamp == job.stamp)
            {
                return;
            }
        }

        s_BakeQueue.push_back(std::move(job));
        if (!s_WorkerBusy)
        {
            s_WorkerBusy = true;
            startWorker = true;
        }
    }
    if (startWorker)
    {
        std::thread(WorkerLoop).detach();
    }
}

bool IsKeyPending(const std::string& key)
{
    std::lock_guard<std::mutex> lock(s_BakeMutex);
    if (s_RunningKey == key)
    {
        return true;
    }
    for (const auto& job : s_BakeQueue)
    {
        if (BakeKey(job.fontGuid, job.fontSize) == key)
        {
            return true;
        }
    }
    for (const auto& done : s_BakeResults)
    {
        if (BakeKey(done.job.fontGuid, done.job.fontSize) == key)
        {
            return true;
        }
    }
    return false;
}

// The sizes of a font that wait, compile or wait to be written.
std::vector<int> PendingSizes(const std::string& fontGuid)
{
    std::vector<int> sizes;
    std::lock_guard<std::mutex> lock(s_BakeMutex);
    for (const auto& job : s_BakeQueue)
    {
        if (job.fontGuid == fontGuid)
        {
            sizes.push_back(job.fontSize);
        }
    }
    for (const auto& done : s_BakeResults)
    {
        if (done.job.fontGuid == fontGuid)
        {
            sizes.push_back(done.job.fontSize);
        }
    }
    if (s_RunningKey.rfind(fontGuid + ":", 0) == 0)
    {
        sizes.push_back(std::atoi(s_RunningKey.c_str() + fontGuid.size() + 1));
    }
    return sizes;
}

// The absolute path of a TrueType font asset, or "" when the GUID is not one.
std::string TtfPathForGuid(const std::string& sourceGuid)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    const DekiEditor::AssetInfo* info = pipeline ? pipeline->GetAssetInfoByGuid(sourceGuid) : nullptr;
    if (!info)
    {
        return "";
    }
    const std::string ext = LowerExtension(info->path);
    if (ext != ".ttf" && ext != ".otf")
    {
        return "";
    }
    return (fs::path(pipeline->GetProjectPath()) / info->path).string();
}

std::vector<int> CachedSizes(const std::string& fontGuid, const std::string& cacheDir)
{
    std::vector<int> sizes;
    for (int size = kMinFontSize; size <= kMaxFontSize; ++size)
    {
        if (fs::exists(cacheDir + "/" + VariantGuid(fontGuid, size)))
        {
            sizes.push_back(size);
        }
    }
    return sizes;
}

}  // namespace

// Registers a font's baked variants as sub-assets and with AssetManager.
// Doing it here lets HandleFontSync run only when the source changes
// (OnSourceChange policy) instead of on every project open.
static void RegisterFontSubAssets(const std::string& fontGuid, const std::string& projectPath)
{
    // The editor defines where the cache lives; a package must not hardcode it.
    std::string cachePath = DekiEditor::GetCacheDirectory(projectPath);
    std::vector<DekiEditor::SubAssetInfo> subAssets;
    int index = 0;

    for (int sz = kMinFontSize; sz <= kMaxFontSize; sz++)
    {
        std::string vGuid = VariantGuid(fontGuid, sz);
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

        std::string aGuid = AtlasGuid(fontGuid, sz);
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

    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (pipeline)
    {
        pipeline->RegisterSubAssets(fontGuid, subAssets);
    }
}

// Font sync: runs when the font file or its .data changes. When the settings
// or the file differ from what the cached sizes were baked from, those sizes
// are deleted, so nothing stale is used or shipped, and baked again in the
// background.
static void HandleFontSync(const std::string& absolutePath, const std::string& fontGuid, const std::string& projectPath)
{
    LoadBakedStamps(projectPath);

    FontCompiler::CompileOptions options = LoadFontCompileOptions(absolutePath);
    const std::string stamp = MakeTtfStamp(absolutePath, options);
    s_CurrentStamps[fontGuid] = stamp;

    auto it = s_BakedStamps.find(fontGuid);
    if (it != s_BakedStamps.end() && it->second == stamp)
    {
        return;
    }

    const std::string cacheDir = DekiEditor::GetCacheDirectory(projectPath);
    std::vector<int> sizes = CachedSizes(fontGuid, cacheDir);
    for (int size : sizes)
    {
        std::error_code ec;
        fs::remove(cacheDir + "/" + VariantGuid(fontGuid, size), ec);
        fs::remove(cacheDir + "/" + AtlasGuid(fontGuid, size), ec);
    }
    // Bakes still under way are made from the old settings: they bake again.
    for (int size : PendingSizes(fontGuid))
    {
        if (std::find(sizes.begin(), sizes.end(), size) == sizes.end())
        {
            sizes.push_back(size);
        }
    }
    s_BakedStamps[fontGuid] = stamp;
    SaveBakedStamps(projectPath);

    for (int size : sizes)
    {
        BakeJob job;
        job.fontGuid = fontGuid;
        job.fontPath = absolutePath;
        job.fontSize = size;
        job.options = options;
        job.stamp = stamp;
        job.replaces = true;
        Enqueue(std::move(job));
    }

    if (!sizes.empty())
    {
        DEKI_LOG_EDITOR("FontSync: %s changed, baking %zu sizes again", absolutePath.c_str(), sizes.size());
    }
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

// BDF sync: a BDF font has one baked form, made from its selected characters
// and decoration. It is small, so it bakes right here; a change to either
// bakes it again.
static void HandleBdfSync(const std::string& absolutePath, const std::string& fontGuid, const std::string& projectPath)
{
    LoadBakedStamps(projectPath);

    std::string dataPath = absolutePath + ".data";
    json j = ReadDataFile(dataPath);
    const json settings = j.contains("bdfSettings") && j["bdfSettings"].is_object() ? j["bdfSettings"] : json::object();

    // The selected characters from .data; all of them when none are listed.
    std::vector<int> selectedChars;
    if (settings.contains("selectedChars") && settings["selectedChars"].is_array())
    {
        for (const auto& val : settings["selectedChars"])
        {
            if (val.is_number_integer())
            {
                selectedChars.push_back(val.get<int>());
            }
        }
    }

    // No settings yet: select every codepoint in the BDF file, and save them
    // so later syncs use them.
    if (selectedChars.empty())
    {
        selectedChars = FontCompiler::GetBdfCodepoints(absolutePath);
        if (selectedChars.empty())
        {
            DEKI_LOG_WARNING("BdfSync: No glyphs found in %s", absolutePath.c_str());
            return;
        }
        std::sort(selectedChars.begin(), selectedChars.end());
        j["bdfSettings"]["selectedChars"] = selectedChars;
        std::ofstream outFile(dataPath);
        if (outFile.is_open())
        {
            outFile << j.dump(2);
        }
    }

    // Decoration settings, in the same form as a TTF's fontSettings.
    FontCompiler::BdfCompileOptions options;
    options.selectedChars = selectedChars;
    options.padding = 2;
    options.maxAtlasSize = 2048;
    options.decoration = DecorationFromSetting(settings.value("decoration", std::string("none")));
    options.outlineSize = ReadClamped(settings, "outlineSize", 1, 1, 3);
    options.shadowDx = ReadClamped(settings, "shadowDx", 1, -3, 3);
    options.shadowDy = ReadClamped(settings, "shadowDy", 1, -3, 3);

    // What the bake is made from: the characters, the decoration and the file.
    uint32_t charsHash = 2166136261u;
    for (int c : selectedChars)
    {
        charsHash = (charsHash ^ static_cast<uint32_t>(c)) * 16777619u;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "bdf%d:%zu:%08x:d%d:%d:%d:%d:", kBakeVersion, selectedChars.size(), charsHash,
                  static_cast<int>(options.decoration), options.outlineSize, options.shadowDx, options.shadowDy);
    const std::string stamp = buf + SourceStamp(absolutePath);

    std::string variantGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf");
    std::string atlasGuid = Deki::GenerateDeterministicGuid(fontGuid + ":bdf:atlas");

    std::string cacheDir = DekiEditor::GetCacheDirectory(projectPath);
    fs::create_directories(cacheDir);

    std::string dfontPath = cacheDir + "/" + variantGuid;
    std::string atlasPath = cacheDir + "/" + atlasGuid;

    // Already cached from these settings. AssetManager and sub-asset
    // registration happen in RegisterBdfSubAssets (OnImportComplete).
    const bool cached = fs::exists(dfontPath) && fs::exists(atlasPath);
    auto it = s_BakedStamps.find(fontGuid);
    const bool current = it != s_BakedStamps.end() && it->second == stamp;
    if (cached && (current || it == s_BakedStamps.end()))
    {
        // A cache from before stamps were kept is taken as current.
        if (!current)
        {
            s_BakedStamps[fontGuid] = stamp;
            SaveBakedStamps(projectPath);
        }
        return;
    }

    std::error_code ec;
    fs::remove(dfontPath, ec);
    fs::remove(atlasPath, ec);

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

    s_BakedStamps[fontGuid] = stamp;
    SaveBakedStamps(projectPath);
    RegisterBdfSubAssets(fontGuid, projectPath);

    if (cached)
    {
        // Texts that loaded the old bake load this one.
        DekiEditor::EditorApplication::Get().RequestAssetInvalidation();
    }
    else
    {
        // Texts that found no font try again.
        Deki::AssetManager::Get()->AdvanceEpoch();
    }
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
                for (int sz = kMinFontSize; sz <= kMaxFontSize; ++sz)
                {
                    valid.insert(VariantGuid(info.guid, sz));
                    valid.insert(AtlasGuid(info.guid, sz));
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
                std::string ext = LowerExtension(path);
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

void EnsureFontSizeBaked(const std::string& sourceGuid, int fontSize, const void* requester)
{
    if (fontSize < kMinFontSize || fontSize > kMaxFontSize)
    {
        return;
    }
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    const std::string fontPath = TtfPathForGuid(sourceGuid);
    if (!pipeline || fontPath.empty())
    {
        return;
    }

    const std::string cacheDir = DekiEditor::GetCacheDirectory(pipeline->GetProjectPath());
    if (fs::exists(cacheDir + "/" + VariantGuid(sourceGuid, fontSize)) &&
        fs::exists(cacheDir + "/" + AtlasGuid(sourceGuid, fontSize)))
    {
        return;
    }

    LoadBakedStamps(pipeline->GetProjectPath());
    const std::string& stamp = CurrentTtfStamp(sourceGuid, fontPath);
    if (s_FailedBakes.count(BakeKey(sourceGuid, fontSize) + ":" + stamp))
    {
        return;
    }

    BakeJob job;
    job.fontGuid = sourceGuid;
    job.fontPath = fontPath;
    job.fontSize = fontSize;
    job.options = LoadFontCompileOptions(fontPath);
    job.stamp = stamp;
    job.requester = requester;
    Enqueue(std::move(job));
}

bool IsFontSizeBaking(const std::string& sourceGuid, int fontSize)
{
    return IsKeyPending(BakeKey(sourceGuid, fontSize));
}

bool DidFontSizeBakeFail(const std::string& sourceGuid, int fontSize)
{
    auto it = s_CurrentStamps.find(sourceGuid);
    return it != s_CurrentStamps.end() && s_FailedBakes.count(BakeKey(sourceGuid, fontSize) + ":" + it->second);
}

std::vector<int> GetBakedFontSizes(const std::string& sourceGuid)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline)
    {
        return {};
    }
    return CachedSizes(sourceGuid, DekiEditor::GetCacheDirectory(pipeline->GetProjectPath()));
}

int GetPendingFontBakeCount(const std::string& sourceGuid)
{
    std::lock_guard<std::mutex> lock(s_BakeMutex);
    int count = 0;
    for (const auto& job : s_BakeQueue)
    {
        count += job.fontGuid == sourceGuid ? 1 : 0;
    }
    for (const auto& done : s_BakeResults)
    {
        count += done.job.fontGuid == sourceGuid ? 1 : 0;
    }
    if (s_RunningKey.rfind(sourceGuid + ":", 0) == 0)
    {
        ++count;
    }
    return count;
}

void RebakeFont(const std::string& sourceGuid)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    const std::string fontPath = TtfPathForGuid(sourceGuid);
    if (!pipeline || fontPath.empty())
    {
        return;
    }
    LoadBakedStamps(pipeline->GetProjectPath());

    // Read the settings and the file again, in case the stamp missed a change.
    FontCompiler::CompileOptions options = LoadFontCompileOptions(fontPath);
    const std::string stamp = MakeTtfStamp(fontPath, options);
    s_CurrentStamps[sourceGuid] = stamp;

    for (int size : GetBakedFontSizes(sourceGuid))
    {
        s_FailedBakes.erase(BakeKey(sourceGuid, size) + ":" + stamp);
        BakeJob job;
        job.fontGuid = sourceGuid;
        job.fontPath = fontPath;
        job.fontSize = size;
        job.options = options;
        job.stamp = stamp;
        job.replaces = true;
        Enqueue(std::move(job));
    }
}

void PumpFontBakes()
{
    if (!s_HaveResults.exchange(false))
    {
        return;
    }

    std::vector<BakeResult> results;
    {
        std::lock_guard<std::mutex> lock(s_BakeMutex);
        results.swap(s_BakeResults);
    }

    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline)
    {
        return;
    }
    const std::string projectPath = pipeline->GetProjectPath();
    const std::string cacheDir = DekiEditor::GetCacheDirectory(projectPath);
    fs::create_directories(cacheDir);
    LoadBakedStamps(projectPath);

    std::unordered_set<std::string> landedFonts;
    bool replaced = false;
    for (auto& done : results)
    {
        const BakeJob& job = done.job;

        // Made from settings that have changed since: a newer bake is queued.
        auto current = s_CurrentStamps.find(job.fontGuid);
        if (current != s_CurrentStamps.end() && current->second != job.stamp)
        {
            continue;
        }
        if (!done.ok)
        {
            s_FailedBakes.insert(BakeKey(job.fontGuid, job.fontSize) + ":" + job.stamp);
            DEKI_LOG_WARNING("FontSync: Failed to bake %s @ %d px", job.fontPath.c_str(), job.fontSize);
            continue;
        }

        const std::string variantGuid = VariantGuid(job.fontGuid, job.fontSize);
        const std::string atlasGuid = AtlasGuid(job.fontGuid, job.fontSize);
        const std::string atlasPath = cacheDir + "/" + atlasGuid;
        const std::string dfontPath = cacheDir + "/" + variantGuid;
        if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, done.result.atlasRGBA.data(), done.result.atlasWidth,
                                                       done.result.atlasHeight, DekiEditor::TextureFormat::ALPHA8) ||
            !FontCompiler::WriteDfontFile(dfontPath, done.result, atlasGuid))
        {
            DEKI_LOG_WARNING("FontSync: Failed to write %s @ %d px to the cache", job.fontPath.c_str(), job.fontSize);
            continue;
        }

        Deki::AssetManager::Get()->RegisterGuid(variantGuid, variantGuid);
        s_BakedStamps[job.fontGuid] = job.stamp;
        landedFonts.insert(job.fontGuid);
        replaced = replaced || job.replaces;
        DEKI_LOG_EDITOR("FontSync: baked %s @ %d px", job.fontPath.c_str(), job.fontSize);
    }

    if (landedFonts.empty())
    {
        return;
    }

    SaveBakedStamps(projectPath);
    for (const std::string& fontGuid : landedFonts)
    {
        RegisterFontSubAssets(fontGuid, projectPath);
    }
    // Play mode resolves fonts through the asset table.
    pipeline->UpdateAssetTable();

    if (replaced)
    {
        // Texts drawing the old bake load the new one.
        DekiEditor::EditorApplication::Get().RequestAssetInvalidation();
    }
    else
    {
        // Texts that found no font try again.
        Deki::AssetManager::Get()->AdvanceEpoch();
    }
}

void StopFontBaker()
{
    std::unique_lock<std::mutex> lock(s_BakeMutex);
    s_Stopping = true;
    s_BakeQueue.clear();
    s_BakeIdle.wait(lock, [] { return !s_WorkerBusy; });
    s_BakeResults.clear();
    s_HaveResults = false;
    s_Stopping = false;
}

FontCompiler::CompileOptions LoadFontCompileOptions(const std::string& fontPath)
{
    FontCompiler::CompileOptions options;
    options.padding = 2;
    options.maxAtlasSize = 2048;

    json j = ReadDataFile(fontPath + ".data");
    if (!j.contains("fontSettings") || !j["fontSettings"].is_object())
    {
        return options;
    }
    const json& settings = j["fontSettings"];
    options.firstChar = ReadClamped(settings, "firstChar", 32, 0, 255);
    options.lastChar = ReadClamped(settings, "lastChar", 126, options.firstChar, 255);
    options.hinting = HintingFromSetting(settings.value("hinting", std::string("light")));
    options.oversample = ReadClamped(settings, "oversample", 2, 1, 4);
    options.decoration = DecorationFromSetting(settings.value("decoration", std::string("none")));
    options.outlineSize = ReadClamped(settings, "outlineSize", 1, 1, 3);
    options.shadowDx = ReadClamped(settings, "shadowDx", 1, -3, 3);
    options.shadowDy = ReadClamped(settings, "shadowDy", 1, -3, 3);
    return options;
}

std::vector<int> LoadFontExtraSizes(const std::string& fontPath)
{
    std::vector<int> sizes;
    json j = ReadDataFile(fontPath + ".data");
    if (!j.contains("fontSettings") || !j["fontSettings"].is_object())
    {
        return sizes;
    }
    const json& settings = j["fontSettings"];
    if (!settings.contains("extraSizes") || !settings["extraSizes"].is_array())
    {
        return sizes;
    }
    for (const auto& size : settings["extraSizes"])
    {
        if (size.is_number_integer() && size.get<int>() >= kMinFontSize && size.get<int>() <= kMaxFontSize)
        {
            sizes.push_back(size.get<int>());
        }
    }
    std::sort(sizes.begin(), sizes.end());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    return sizes;
}

}  // namespace Deki2D

#endif  // DEKI_EDITOR
