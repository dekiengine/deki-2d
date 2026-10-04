#include "FontFileInspector.h"
#include <deki-editor/Paths.h>

#ifdef DEKI_EDITOR

#include "FontCompiler.h"
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/AssetData.h>
#include <deki-editor/EditorApplication.h>
#include <deki-editor/EditorUI.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>

// OpenGL for preview texture
#ifdef _WIN32
// NOMINMAX: windows.h's min/max macros would break std::min/std::max in every
// file sharing this translation unit (unity build).
#define NOMINMAX
#include <windows.h>
#endif
#include <GL/gl.h>

#include <deki/LogSystem.h>
#include <deki/Guid.h>                 // For Deki::GenerateDeterministicGuid
#include <deki/assets/AssetManager.h>  // For registering variant GUIDs

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Deki2D
{

static const char* s_FontExtensions[] = { ".ttf", ".otf" };

// Hinting mode to and from its JSON string.
static const char* HintingToString(FontCompiler::HintingMode m)
{
    switch (m)
    {
        case FontCompiler::HintingMode::None: return "none";
        case FontCompiler::HintingMode::Normal: return "normal";
        case FontCompiler::HintingMode::Mono: return "mono";
        case FontCompiler::HintingMode::Light:
        default: return "light";
    }
}

static FontCompiler::HintingMode HintingFromString(const std::string& s)
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

// Decoration mode to and from its JSON string.
static const char* DecorationToString(FontCompiler::DecorationMode m)
{
    switch (m)
    {
        case FontCompiler::DecorationMode::Outline: return "outline";
        case FontCompiler::DecorationMode::Shadow: return "shadow";
        case FontCompiler::DecorationMode::None:
        default: return "none";
    }
}

static FontCompiler::DecorationMode DecorationFromString(const std::string& s)
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

const char** FontFileInspector::GetExtensions() const
{
    return s_FontExtensions;
}

int FontFileInspector::GetExtensionCount() const
{
    return 2;
}

void FontFileInspector::OnInspectorGUI(const std::string& assetPath, const std::string& assetGuid)
{
    auto& ui = DekiEditor::EditorUI::Get();

    if (m_CurrentAssetPath != assetPath)
    {
        DEKI_LOG_DEBUG("FontInspector: Asset changed, loading settings for %s (guid=%s)", assetPath.c_str(),
                       assetGuid.c_str());
        LoadSettings(assetPath);
        m_CurrentAssetPath = assetPath;
        m_SettingsModified = false;
        m_VariantCacheDirty = true;
    }

    fs::path path(assetPath);
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Font: %s", path.filename().string().c_str());
        ui.Text(buf);
    }

    if (!assetGuid.empty())
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "GUID: %s", assetGuid.c_str());
        ui.TextDisabled(buf);
    }

    ui.Separator();

    if (ui.CollapsingHeader("Character Range", nullptr, true))
    {
        ui.SetNextItemWidth(100);
        if (ui.InputInt("First Character", &m_FirstChar))
        {
            m_FirstChar = (std::max)(0, (std::min)(255, m_FirstChar));
            m_SettingsModified = true;
        }
        ui.SameLine();
        if (m_FirstChar >= 32 && m_FirstChar <= 126)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "('%c')", static_cast<char>(m_FirstChar));
            ui.Text(buf);
        }

        ui.SetNextItemWidth(100);
        if (ui.InputInt("Last Character", &m_LastChar))
        {
            m_LastChar = (std::max)(m_FirstChar, (std::min)(255, m_LastChar));
            m_SettingsModified = true;
        }
        ui.SameLine();
        if (m_LastChar >= 32 && m_LastChar <= 126)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "('%c')", static_cast<char>(m_LastChar));
            ui.Text(buf);
        }

        int charCount = m_LastChar - m_FirstChar + 1;
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Total: %d characters", charCount);
            ui.TextDisabled(buf);
        }
    }

    if (ui.CollapsingHeader("Rasterization", nullptr, true))
    {
        static const char* const kHintingLabels[] = { "None (unhinted AA)", "Light (smoother AA)",
                                                      "Normal (FreeType default)", "Mono (1-bit, no AA)" };
        static const FontCompiler::HintingMode kHintingValues[] = {
            FontCompiler::HintingMode::None,
            FontCompiler::HintingMode::Light,
            FontCompiler::HintingMode::Normal,
            FontCompiler::HintingMode::Mono,
        };
        int currentIndex = 1;  // Light
        for (int i = 0; i < 4; ++i)
        {
            if (kHintingValues[i] == m_Hinting)
            {
                currentIndex = i;
                break;
            }
        }
        ui.SetNextItemWidth(220);
        if (ui.Combo("Hinting Mode", &currentIndex, kHintingLabels, 4))
        {
            m_Hinting = kHintingValues[currentIndex];
            m_SettingsModified = true;
        }

        // Oversampling is off with Mono hinting: a 1-bit source gains nothing.
        static const char* const kOversampleLabels[] = { "1x (off)", "2x", "3x", "4x" };
        int oversampleIndex = m_Oversample - 1;
        if (oversampleIndex < 0)
        {
            oversampleIndex = 0;
        }
        if (oversampleIndex > 3)
        {
            oversampleIndex = 3;
        }
        const bool oversampleDisabled = (m_Hinting == FontCompiler::HintingMode::Mono);
        if (oversampleDisabled)
        {
            m_Oversample = 1;
            oversampleIndex = 0;
            ui.BeginDisabled();
        }
        ui.SetNextItemWidth(220);
        if (ui.Combo("Oversample", &oversampleIndex, kOversampleLabels, 4))
        {
            m_Oversample = oversampleIndex + 1;
            m_SettingsModified = true;
        }
        if (oversampleDisabled)
        {
            ui.EndDisabled();
        }

        // Decoration: a baked outline or drop shadow, as on the Nintendo DS.
        static const char* const kDecorationLabels[] = { "None", "Outline", "Shadow" };
        static const FontCompiler::DecorationMode kDecorationValues[] = {
            FontCompiler::DecorationMode::None,
            FontCompiler::DecorationMode::Outline,
            FontCompiler::DecorationMode::Shadow,
        };
        int decorationIndex = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (kDecorationValues[i] == m_Decoration)
            {
                decorationIndex = i;
                break;
            }
        }
        ui.SetNextItemWidth(220);
        if (ui.Combo("Decoration", &decorationIndex, kDecorationLabels, 3))
        {
            m_Decoration = kDecorationValues[decorationIndex];
            m_SettingsModified = true;
        }
        if (m_Decoration == FontCompiler::DecorationMode::Outline)
        {
            ui.SetNextItemWidth(100);
            if (ui.InputInt("Outline Size (px)", &m_OutlineSize))
            {
                if (m_OutlineSize < 1)
                {
                    m_OutlineSize = 1;
                }
                if (m_OutlineSize > 3)
                {
                    m_OutlineSize = 3;
                }
                m_SettingsModified = true;
            }
        }
        else if (m_Decoration == FontCompiler::DecorationMode::Shadow)
        {
            ui.SetNextItemWidth(100);
            if (ui.InputInt("Shadow dX", &m_ShadowDx))
            {
                if (m_ShadowDx < -3)
                {
                    m_ShadowDx = -3;
                }
                if (m_ShadowDx > 3)
                {
                    m_ShadowDx = 3;
                }
                m_SettingsModified = true;
            }
            ui.SetNextItemWidth(100);
            if (ui.InputInt("Shadow dY", &m_ShadowDy))
            {
                if (m_ShadowDy < -3)
                {
                    m_ShadowDy = -3;
                }
                if (m_ShadowDy > 3)
                {
                    m_ShadowDy = 3;
                }
                m_SettingsModified = true;
            }
        }

        ui.TextDisabled("Affects edge smoothness. Re-bake variants after changing.");
    }

    if (ui.CollapsingHeader("Size Variants", nullptr, true))
    {
        ui.TextDisabled("Configure which sizes to bake for this font.");
        ui.Spacing();

        int indexToRemove = -1;
        for (size_t i = 0; i < m_Sizes.size(); ++i)
        {
            ui.PushID(static_cast<int>(i));

            // The variant GUID comes from the .data file when it has one.
            std::string variantGuid = GetVariantGuidFromData(assetPath, m_Sizes[i]);
            if (variantGuid.empty())
            {
                variantGuid = "(not saved)";
            }
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d px", m_Sizes[i]);
                ui.Text(buf);
            }
            ui.SameLine();
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "(%s...)",
                              variantGuid.length() > 8 ? variantGuid.substr(0, 8).c_str() : variantGuid.c_str());
                ui.TextDisabled(buf);
            }

            ui.SameLine();
            if (ui.SmallButton("X"))
            {
                indexToRemove = static_cast<int>(i);
            }

            ui.PopID();
        }

        if (indexToRemove >= 0)
        {
            m_Sizes.erase(m_Sizes.begin() + indexToRemove);
            m_SettingsModified = true;
            m_VariantCacheDirty = true;
        }

        ui.Separator();
        ui.SetNextItemWidth(80);
        ui.InputInt("##NewSize", &m_NewSize);
        m_NewSize = (std::max)(6, (std::min)(128, m_NewSize));

        ui.SameLine();
        bool sizeExists = std::find(m_Sizes.begin(), m_Sizes.end(), m_NewSize) != m_Sizes.end();
        if (sizeExists)
        {
            ui.BeginDisabled();
            ui.Button("Add Size");
            ui.EndDisabled();
            ui.SameLine();
            ui.TextDisabled("(already exists)");
        }
        else if (ui.Button("Add Size"))
        {
            m_Sizes.push_back(m_NewSize);
            std::sort(m_Sizes.begin(), m_Sizes.end());
            m_SettingsModified = true;
            m_VariantCacheDirty = true;
        }
    }

    // The "is it cached" map is refreshed only when something changed, not every frame.
    if (m_VariantCacheDirty)
    {
        RefreshVariantCache(assetPath);
        m_VariantCacheDirty = false;
    }

    bool hasUncachedVariants = false;
    for (int size : m_Sizes)
    {
        auto it = m_VariantCached.find(size);
        if (it == m_VariantCached.end() || !it->second)
        {
            hasUncachedVariants = true;
            break;
        }
    }

    // Apply is enabled when the settings changed or a variant is not cached.
    ui.Separator();
    bool canBake = m_SettingsModified || hasUncachedVariants;
    if (canBake && !m_Sizes.empty())
    {
        if (ui.Button("Apply & Bake Fonts"))
        {
            SaveSettings(assetPath, assetGuid);

            for (int size : m_Sizes)
            {
                BakeFontVariant(assetPath, assetGuid, size);
            }

            m_SettingsModified = false;
            m_VariantCacheDirty = true;

            // Invalidate all assets so the scene view picks up the re-baked fonts.
            DekiEditor::EditorApplication::Get().RequestAssetInvalidation();
        }
        if (m_SettingsModified)
        {
            ui.SameLine();
            ui.TextColored(1.0f, 0.8f, 0.2f, 1.0f, "Unsaved changes");
        }
        else if (hasUncachedVariants)
        {
            ui.SameLine();
            ui.TextColored(0.9f, 0.6f, 0.2f, 1.0f, "Not cached");
        }
    }
    else
    {
        ui.BeginDisabled();
        ui.Button("Apply & Bake Fonts");
        ui.EndDisabled();
    }

    // Rebake deletes the cached files, then bakes every variant again.
    if (!m_Sizes.empty())
    {
        ui.SameLine();
        if (ui.Button("Rebake"))
        {
            SaveSettings(assetPath, assetGuid);

            auto* pl = DekiEditor::AssetPipeline::Instance();
            if (pl)
            {
                std::string cacheDir = pl->GetProjectPath() + "/cache/";
                for (int size : m_Sizes)
                {
                    std::string variantGuid = GetVariantGuidFromData(assetPath, size);
                    std::string atlasGuid = GetAtlasGuidFromData(assetPath, size);
                    std::error_code ec;
                    if (!variantGuid.empty())
                    {
                        fs::remove(cacheDir + variantGuid, ec);
                    }
                    if (!atlasGuid.empty())
                    {
                        fs::remove(cacheDir + atlasGuid, ec);
                    }
                }
            }

            for (int size : m_Sizes)
            {
                BakeFontVariant(assetPath, assetGuid, size);
            }

            m_SettingsModified = false;
            m_VariantCacheDirty = true;
            DekiEditor::EditorApplication::Get().RequestAssetInvalidation();
        }
        if (ui.IsItemHovered())
        {
            ui.SetTooltip("Delete cached fonts and re-bake all variants");
        }
    }

    if (!m_Sizes.empty())
    {
        ui.Separator();
        if (ui.CollapsingHeader("Variant Status"))
        {
            for (int size : m_Sizes)
            {
                auto it = m_VariantCached.find(size);
                bool cached = (it != m_VariantCached.end()) && it->second;

                if (cached)
                {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%d px: Cached", size);
                    ui.TextColored(0.4f, 0.9f, 0.4f, 1.0f, buf);
                }
                else
                {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%d px: Not cached", size);
                    ui.TextColored(0.9f, 0.6f, 0.2f, 1.0f, buf);
                }
            }
        }
    }

    ui.Separator();
    if (ui.CollapsingHeader("Preview", nullptr, true))
    {
        ui.SetNextItemWidth(80);
        ui.InputInt("Preview Size", &m_PreviewSize);
        m_PreviewSize = (std::max)(6, (std::min)(128, m_PreviewSize));

        ui.SetNextItemWidth(200);
        ui.InputText("Preview Text", m_PreviewText, sizeof(m_PreviewText));

        if (ui.Button("Generate Preview"))
        {
            GeneratePreview(assetPath, m_PreviewSize);
        }

        if (m_PreviewTextureId != 0 && m_PreviewTextureWidth > 0 && m_PreviewTextureHeight > 0)
        {
            ui.SameLine();

            // Save adds this size to the list and bakes it.
            bool sizeExists = std::find(m_Sizes.begin(), m_Sizes.end(), m_PreviewSize) != m_Sizes.end();
            if (sizeExists)
            {
                ui.BeginDisabled();
                ui.Button("Save This Size");
                ui.EndDisabled();
                ui.SameLine();
                ui.TextDisabled("(already saved)");
            }
            else if (ui.Button("Save This Size"))
            {
                m_Sizes.push_back(m_PreviewSize);
                std::sort(m_Sizes.begin(), m_Sizes.end());
                m_SettingsModified = true;

                SaveSettings(assetPath, assetGuid);
                BakeFontVariant(assetPath, assetGuid, m_PreviewSize);
                m_SettingsModified = false;
                m_VariantCacheDirty = true;

                // Invalidate all assets so the scene view picks up the new font.
                DekiEditor::EditorApplication::Get().RequestAssetInvalidation();
            }

            ui.Spacing();
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Preview at %d px:", m_LastPreviewSize);
                ui.Text(buf);
            }

            ui.Image(m_PreviewTextureId, (float)m_PreviewTextureWidth, (float)m_PreviewTextureHeight);
        }
    }

    // Drop the preview when another asset is shown.
    if (m_LastPreviewAsset != assetPath)
    {
        CleanupPreview();
        m_LastPreviewAsset = assetPath;
    }
}

void FontFileInspector::RefreshVariantCache(const std::string& assetPath)
{
    m_VariantCached.clear();

    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline || m_Sizes.empty())
    {
        return;
    }

    const std::string cacheDir = pipeline->GetProjectPath() + "/cache/";
    for (int size : m_Sizes)
    {
        const std::string variantGuid = GetVariantGuidFromData(assetPath, size);
        if (variantGuid.empty())
        {
            m_VariantCached[size] = false;
            continue;
        }

        std::string cachePath = cacheDir + variantGuid;
        for (char& c : cachePath)
        {
            if (c == '/')
            {
                c = '\\';
            }
        }

        m_VariantCached[size] = fs::exists(cachePath);
    }
}

void FontFileInspector::LoadSettings(const std::string& assetPath)
{
    m_Sizes.clear();
    m_FirstChar = 32;
    m_LastChar = 126;
    m_Hinting = FontCompiler::HintingMode::Light;
    m_Oversample = 2;
    m_Decoration = FontCompiler::DecorationMode::None;
    m_OutlineSize = 1;
    m_ShadowDx = 1;
    m_ShadowDy = 1;

    std::string dataPath = assetPath + ".data";
    if (!fs::exists(dataPath))
    {
        return;
    }

    std::ifstream file(dataPath);
    if (!file.is_open())
    {
        return;
    }

    try
    {
        json j = json::parse(file);

        if (j.contains("fontSettings") && j["fontSettings"].is_object())
        {
            auto& settings = j["fontSettings"];

            if (settings.contains("sizes") && settings["sizes"].is_array())
            {
                for (const auto& size : settings["sizes"])
                {
                    if (size.is_number_integer())
                    {
                        m_Sizes.push_back(size.get<int>());
                    }
                }
            }

            if (settings.contains("firstChar") && settings["firstChar"].is_number_integer())
            {
                m_FirstChar = settings["firstChar"].get<int>();
            }

            if (settings.contains("lastChar") && settings["lastChar"].is_number_integer())
            {
                m_LastChar = settings["lastChar"].get<int>();
            }

            if (settings.contains("hinting") && settings["hinting"].is_string())
            {
                m_Hinting = HintingFromString(settings["hinting"].get<std::string>());
            }

            if (settings.contains("oversample") && settings["oversample"].is_number_integer())
            {
                int v = settings["oversample"].get<int>();
                if (v < 1)
                {
                    v = 1;
                }
                if (v > 4)
                {
                    v = 4;
                }
                m_Oversample = v;
            }

            if (settings.contains("decoration") && settings["decoration"].is_string())
            {
                m_Decoration = DecorationFromString(settings["decoration"].get<std::string>());
            }
            if (settings.contains("outlineSize") && settings["outlineSize"].is_number_integer())
            {
                int v = settings["outlineSize"].get<int>();
                if (v < 1)
                {
                    v = 1;
                }
                if (v > 3)
                {
                    v = 3;
                }
                m_OutlineSize = v;
            }
            if (settings.contains("shadowDx") && settings["shadowDx"].is_number_integer())
            {
                int v = settings["shadowDx"].get<int>();
                if (v < -3)
                {
                    v = -3;
                }
                if (v > 3)
                {
                    v = 3;
                }
                m_ShadowDx = v;
            }
            if (settings.contains("shadowDy") && settings["shadowDy"].is_number_integer())
            {
                int v = settings["shadowDy"].get<int>();
                if (v < -3)
                {
                    v = -3;
                }
                if (v > 3)
                {
                    v = 3;
                }
                m_ShadowDy = v;
            }
        }
    }
    catch (const json::exception&)
    {
        // A file that does not parse is ignored.
    }
}

void FontFileInspector::SaveSettings(const std::string& assetPath, const std::string& assetGuid)
{
    std::string dataPath = assetPath + ".data";

    // Start from the existing .data file, if any.
    json j;
    if (fs::exists(dataPath))
    {
        std::ifstream file(dataPath);
        if (file.is_open())
        {
            try
            {
                j = json::parse(file);
            }
            catch (const json::exception&)
            {
                j = json::object();
            }
        }
    }

    if (!assetGuid.empty())
    {
        j["guid"] = assetGuid;
    }

    j["fontSettings"] = json::object();
    j["fontSettings"]["sizes"] = m_Sizes;
    j["fontSettings"]["firstChar"] = m_FirstChar;
    j["fontSettings"]["lastChar"] = m_LastChar;
    j["fontSettings"]["hinting"] = HintingToString(m_Hinting);
    j["fontSettings"]["oversample"] = m_Oversample;
    j["fontSettings"]["decoration"] = DecorationToString(m_Decoration);
    j["fontSettings"]["outlineSize"] = m_OutlineSize;
    j["fontSettings"]["shadowDx"] = m_ShadowDx;
    j["fontSettings"]["shadowDy"] = m_ShadowDy;
    // Variant GUIDs, derived from the font GUID and the size, so they are the
    // same on every save.
    if (!j.contains("variants"))
    {
        j["variants"] = json::object();
    }

    for (int size : m_Sizes)
    {
        std::string sizeKey = std::to_string(size);

        if (!j["variants"].contains(sizeKey))
        {
            j["variants"][sizeKey] = json::object();
        }

        // From "fontGuid:size".
        std::string variantGuid = Deki::GenerateDeterministicGuid(assetGuid + ":" + sizeKey);
        j["variants"][sizeKey]["guid"] = variantGuid;

        // From "fontGuid:size:atlas".
        std::string atlasGuid = Deki::GenerateDeterministicGuid(assetGuid + ":" + sizeKey + ":atlas");
        j["variants"][sizeKey]["atlasGuid"] = atlasGuid;
    }

    std::ofstream outFile(dataPath);
    if (outFile.is_open())
    {
        outFile << j.dump(2);
    }

    // Tell the AssetPipeline, so it reads the sidecar again.
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (pipeline)
    {
        std::string assetsPath = pipeline->GetProjectPath() + "/project/assets";
        std::string relativePath = fs::relative(assetPath, assetsPath).string();
        for (char& c : relativePath)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }

        pipeline->RefreshAsset("project/assets/" + relativePath);
    }
}

std::string FontFileInspector::GenerateVariantGuid(const std::string& fontGuid, int fontSize)
{
    // The same GUID SaveSettings writes for this size.
    std::string seed = fontGuid + ":" + std::to_string(fontSize);
    return Deki::GenerateDeterministicGuid(seed);
}

std::string FontFileInspector::GetVariantGuidFromData(const std::string& assetPath, int fontSize)
{
    std::string dataPath = assetPath + ".data";
    if (!fs::exists(dataPath))
    {
        return "";
    }

    std::ifstream file(dataPath);
    if (!file.is_open())
    {
        return "";
    }

    try
    {
        json j = json::parse(file);
        std::string sizeKey = std::to_string(fontSize);

        if (j.contains("variants") && j["variants"].contains(sizeKey) && j["variants"][sizeKey].contains("guid"))
        {
            return j["variants"][sizeKey]["guid"].get<std::string>();
        }
    }
    catch (const json::exception&)
    {
        // A file that does not parse is ignored.
    }

    return "";
}

std::string FontFileInspector::GetAtlasGuidFromData(const std::string& assetPath, int fontSize)
{
    std::string dataPath = assetPath + ".data";
    if (!fs::exists(dataPath))
    {
        return "";
    }

    std::ifstream file(dataPath);
    if (!file.is_open())
    {
        return "";
    }

    try
    {
        json j = json::parse(file);
        std::string sizeKey = std::to_string(fontSize);

        if (j.contains("variants") && j["variants"].contains(sizeKey) && j["variants"][sizeKey].contains("atlasGuid"))
        {
            return j["variants"][sizeKey]["atlasGuid"].get<std::string>();
        }
    }
    catch (const json::exception&)
    {
        // A file that does not parse is ignored.
    }

    return "";
}

void FontFileInspector::BakeFontVariant(const std::string& assetPath, const std::string& fontGuid, int fontSize)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline)
    {
        return;
    }

    // SaveSettings has written the GUIDs to the .data file.
    std::string variantGuid = GetVariantGuidFromData(assetPath, fontSize);
    std::string atlasGuid = GetAtlasGuidFromData(assetPath, fontSize);

    if (variantGuid.empty() || atlasGuid.empty())
    {
        DEKI_LOG_ERROR("Failed to get variant/atlas GUIDs from .data file for %s @ %d px", assetPath.c_str(), fontSize);
        return;
    }

    std::string projectPath = pipeline->GetProjectPath();
    std::string cacheDir = DekiEditor::GetCacheDirectory(projectPath);

    fs::create_directories(cacheDir);

    FontCompiler::CompileOptions options;
    options.fontSize = fontSize;
    options.firstChar = m_FirstChar;
    options.lastChar = m_LastChar;
    options.padding = 2;
    options.maxAtlasSize = 2048;
    options.hinting = m_Hinting;
    options.oversample = m_Oversample;
    options.decoration = m_Decoration;
    options.outlineSize = m_OutlineSize;
    options.shadowDx = m_ShadowDx;
    options.shadowDy = m_ShadowDy;

    FontCompiler::CompileResult result;
    if (!FontCompiler::CompileTrueTypeFont(assetPath, options, result))
    {
        DEKI_LOG_ERROR("Failed to compile font variant: %s @ %d px", assetPath.c_str(), fontSize);
        return;
    }

    // The atlas as DTEX, named by atlasGuid with no extension.
    std::string atlasPath = cacheDir + "/" + atlasGuid;
    if (!DekiEditor::TextureImporter::WriteTexFile(atlasPath, result.atlasRGBA.data(), result.atlasWidth,
                                                   result.atlasHeight, DekiEditor::TextureFormat::ALPHA8))
    {
        DEKI_LOG_ERROR("Failed to write font atlas: %s", atlasPath.c_str());
        return;
    }

    // The .dfont, named by variantGuid with no extension. It names its atlas
    // by atlasGuid alone.
    std::string dfontPath = cacheDir + "/" + variantGuid;
    if (!FontCompiler::WriteDfontFile(dfontPath, result, atlasGuid))
    {
        DEKI_LOG_ERROR("Failed to write dfont file: %s", dfontPath.c_str());
        return;
    }

    // Registered so the AssetManager finds the variant at runtime. The file
    // name is the GUID alone; the AssetManager adds the cache directory.
    Deki::AssetManager::Get()->RegisterGuid(variantGuid, variantGuid);

    DEKI_LOG_EDITOR("Baked font variant: %s @ %d px -> font=%s, atlas=%s", assetPath.c_str(), fontSize,
                    variantGuid.c_str(), atlasGuid.c_str());
}

void FontFileInspector::GeneratePreview(const std::string& assetPath, int fontSize)
{
    FontCompiler::CompileOptions options;
    options.fontSize = fontSize;
    options.firstChar = m_FirstChar;
    options.lastChar = m_LastChar;
    options.padding = 2;
    options.maxAtlasSize = 2048;
    options.hinting = m_Hinting;
    options.oversample = m_Oversample;
    options.decoration = m_Decoration;
    options.outlineSize = m_OutlineSize;
    options.shadowDx = m_ShadowDx;
    options.shadowDy = m_ShadowDy;

    FontCompiler::CompileResult result;
    if (!FontCompiler::CompileTrueTypeFont(assetPath, options, result))
    {
        DEKI_LOG_ERROR("Failed to compile font for preview: %s @ %d px", assetPath.c_str(), fontSize);
        return;
    }

    const char* text = m_PreviewText;
    int textLen = static_cast<int>(strlen(text));
    if (textLen == 0)
    {
        text = "Preview";
    }

    int textWidth = 0;
    int textHeight = result.lineHeight;
    for (int i = 0; text[i] != '\0'; ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= result.firstChar && c <= result.lastChar)
        {
            const GlyphInfo& glyph = result.glyphs[c - result.firstChar];
            textWidth += glyph.advance;
        }
    }

    // 4 px of padding on each side.
    int bufferWidth = textWidth + 8;
    int bufferHeight = textHeight + 8;

    std::vector<uint8_t> buffer(bufferWidth * bufferHeight * 4, 0);

    // Dark background.
    for (int i = 0; i < bufferWidth * bufferHeight; ++i)
    {
        buffer[i * 4 + 0] = 40;   // R
        buffer[i * 4 + 1] = 40;   // G
        buffer[i * 4 + 2] = 40;   // B
        buffer[i * 4 + 3] = 255;  // A
    }

    int x = 4;
    int y = 4 + result.baseline;
    for (int i = 0; text[i] != '\0'; ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= result.firstChar && c <= result.lastChar)
        {
            const GlyphInfo& glyph = result.glyphs[c - result.firstChar];

            int destX = x + glyph.offsetX;
            int destY = y + glyph.offsetY;

            for (int gy = 0; gy < glyph.height; ++gy)
            {
                for (int gx = 0; gx < glyph.width; ++gx)
                {
                    int srcX = glyph.x + gx;
                    int srcY = glyph.y + gy;
                    int dstX = destX + gx;
                    int dstY = destY + gy;

                    if (dstX >= 0 && dstX < bufferWidth && dstY >= 0 && dstY < bufferHeight)
                    {
                        int srcIdx = (srcY * result.atlasWidth + srcX) * 4;
                        int dstIdx = (dstY * bufferWidth + dstX) * 4;

                        // White text, with the atlas alpha.
                        uint8_t alpha = result.atlasRGBA[srcIdx + 3];
                        if (alpha > 0)
                        {
                            buffer[dstIdx + 0] = 255;  // R
                            buffer[dstIdx + 1] = 255;  // G
                            buffer[dstIdx + 2] = 255;  // B
                            buffer[dstIdx + 3] = alpha;
                        }
                    }
                }
            }

            x += glyph.advance;
        }
    }

    if (m_PreviewTextureId == 0)
    {
        glGenTextures(1, &m_PreviewTextureId);
    }

    glBindTexture(GL_TEXTURE_2D, m_PreviewTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Unpack state is global, and other GL users (ImGui glyph uploads, say) can
    // leave a row stride set that would shear or overread this tight upload.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bufferWidth, bufferHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

    m_PreviewTextureWidth = bufferWidth;
    m_PreviewTextureHeight = bufferHeight;
    m_LastPreviewSize = fontSize;

    DEKI_LOG_DEBUG("Generated font preview: %s @ %d px (%dx%d)", assetPath.c_str(), fontSize, bufferWidth,
                   bufferHeight);
}

void FontFileInspector::CleanupPreview()
{
    if (m_PreviewTextureId != 0)
    {
        glDeleteTextures(1, &m_PreviewTextureId);
        m_PreviewTextureId = 0;
    }
    m_PreviewTextureWidth = 0;
    m_PreviewTextureHeight = 0;
    m_LastPreviewSize = 0;
}

static FontFileInspector s_FontFileInspector;

void RegisterFontFileInspector()
{
    DekiEditor::FileInspectorRegistry::Instance().Register(&s_FontFileInspector);
}

}  // namespace Deki2D

#endif  // DEKI_EDITOR
